#include "common.hpp"
#include "sockets.hpp"
#include "ip/crc.hpp"
#include "ip/fec_codec.hpp"
#include "ip/ip_layer.hpp"
#include "ip/ip_nat.hpp"
#include "ip/tun_layer.hpp"
#include "ip/utils.hpp"

#include <SDL2/SDL_stdinc.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <linux/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <queue>
#include <random>
#include <string>
#include <sys/poll.h>
#include <thread>
#include <vector>

static std::mutex g_state_mtx;
static std::queue<std::vector<uint8_t>> g_ctrl_tx_queue;

void enqueue_ctrl_frame(std::vector<uint8_t> frame)
{
    std::scoped_lock lock(g_state_mtx);
    g_ctrl_tx_queue.push(std::move(frame));
}

static std::unordered_map<uint16_t, ClientSession> g_clients_by_id;
static std::unordered_map<uint32_t, uint16_t> g_id_by_ip;
static uint16_t g_next_client_id = 1;

static std::atomic<bool> g_ue_registered{ false };
static std::atomic<uint16_t> g_ue_my_id{ UNASSIGNED_ID };
static uint32_t g_ue_token = 0;

void run_tun_tx(SharedData &data)
{
    data.tun = std::make_shared<TunDevice>(data.tun_name);

    if (data.tun->get_fd() < 0)
    {
        logs::tun.error("[TX] Failed to create TUN device {}", data.tun_name);
        return;
    }

    data.tun_name = data.tun->get_name();
    const bool is_bs = data.ip_addr.ends_with(".1");

    if (is_bs)
    {
        if (data.tun->set_ip_and_up(data.ip_addr, "255.255.255.0"))
        {
            logs::tun.info("[BS] Started with IP {}, configuring NAT", data.ip_addr);
            enable_nat(data.tun_name);
        }
    }
    else
    {
        std::random_device rd;
        g_ue_token = static_cast<uint32_t>(rd());
        logs::tun.info("[UE] Waiting for registration with BS. Token: 0x{:08X}", g_ue_token);
    }

    const auto mtu = calculate_mtu(data);
    logs::tun.info("MTU: {}", mtu);

    if (mtu > 0)
        data.tun->set_mtu(static_cast<int>(1500));

    const size_t max_frame_bytes = sizeof(FrameHeader) + mtu;
    uint8_t buffer[1500];
    uint16_t packet_seq = 0;

    auto last_join_send = std::chrono::steady_clock::now() - std::chrono::seconds(2);

    auto tun = data.tun;
    while (!data.stop.load())
    {
        if (!is_bs && !g_ue_registered.load())
        {
            auto now = std::chrono::steady_clock::now();
            if (now - last_join_send > std::chrono::milliseconds(3000))
            {
                last_join_send = now;
                logs::tun.debug("[UE] Sending JOIN request (token 0x{:08X})...", g_ue_token);

                JoinPayload payload{ g_ue_token, 0 };
                std::vector<uint8_t> p_bytes(
                    reinterpret_cast<uint8_t *>(&payload),
                    reinterpret_cast<uint8_t *>(&payload) + sizeof(JoinPayload)
                );

                auto crc = calculateCRC16(p_bytes);
                p_bytes.insert(p_bytes.end(), crc.begin(), crc.end());

                FrameHeader join_hdr{};
                join_hdr.magic = htons(FRAME_MAGIC);
                join_hdr.type = static_cast<uint8_t>(FrameType::JOIN);
                join_hdr.src_id = htons(UNASSIGNED_ID);
                join_hdr.dest_id = htons(BS_USER_ID);
                join_hdr.seq = htons(packet_seq++);
                join_hdr.length = htons(static_cast<uint16_t>(p_bytes.size()));

                std::vector<uint8_t> frame(sizeof(FrameHeader) + p_bytes.size());
                memcpy(frame.data(), &join_hdr, sizeof(FrameHeader));
                memcpy(frame.data() + sizeof(FrameHeader), p_bytes.data(), p_bytes.size());

                send_phy_frame(data, frame, max_frame_bytes);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        {
            std::vector<uint8_t> ctrl_frame;
            {
                std::scoped_lock lock(g_state_mtx);
                if (!g_ctrl_tx_queue.empty())
                {
                    ctrl_frame = std::move(g_ctrl_tx_queue.front());
                    g_ctrl_tx_queue.pop();
                }
            }
            if (!ctrl_frame.empty())
            {
                send_phy_frame(data, ctrl_frame, max_frame_bytes);
                continue;
            }
        }

        struct pollfd pfd = { tun->get_fd(), POLLIN, 0 };
        int ret = poll(&pfd, 1, 100);

        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        if (ret == 0)
            continue;

        if (pfd.revents & POLLIN)
        {
            ssize_t nbytes = tun->read_packet(buffer, sizeof(buffer));
            if (nbytes < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                logs::tun.error("[{}] Read error {}: {}", tun->get_name(), errno, strerror(errno));
                continue;
            }

            if (nbytes > 0)
            {
                std::vector<uint8_t> payload(buffer, buffer + nbytes);

                uint16_t target_dest_id = BS_USER_ID;
                if (is_bs && nbytes >= 20)
                {
                    uint32_t dest_ip = *reinterpret_cast<uint32_t *>(buffer + 16);
                    std::scoped_lock lock(g_state_mtx);
                    if (g_id_by_ip.contains(dest_ip))
                        target_dest_id = g_id_by_ip[dest_ip];
                    else
                        target_dest_id = BROADCAST_USER_ID;
                }

                auto crc = calculateCRC16(payload);
                payload.insert(payload.end(), crc.begin(), crc.end());

                size_t total_len = payload.size();
                size_t offset = 0;
                uint16_t my_id = is_bs ? BS_USER_ID : g_ue_my_id.load();

                while (offset < total_len)
                {
                    size_t chunk_size = std::min<size_t>(mtu, total_len - offset);
                    uint8_t hflag = 0;
                    if (offset == 0)
                        hflag |= FLAG_FIRST;
                    if (offset + chunk_size >= total_len)
                        hflag |= FLAG_LAST;

                    FrameHeader hdr{};
                    hdr.magic = htons(FRAME_MAGIC);
                    hdr.type = static_cast<uint8_t>(FrameType::DATA);
                    hdr.src_id = htons(my_id);
                    hdr.dest_id = htons(target_dest_id);
                    hdr.length = htons(static_cast<uint16_t>(chunk_size));
                    hdr.seq = htons(packet_seq++);
                    hdr.flags = hflag;

                    std::vector<uint8_t> frame;
                    auto *hdr_ptr = reinterpret_cast<uint8_t *>(&hdr);
                    frame.insert(frame.end(), hdr_ptr, hdr_ptr + sizeof(FrameHeader));
                    frame.insert(frame.end(), payload.begin() + offset, payload.begin() + offset + chunk_size);

                    send_phy_frame(data, frame, max_frame_bytes);
                    offset += chunk_size;
                }
            }
        }
    }
}

void run_tun_rx(SharedData &data)
{
    while (!data.stop.load() && (!data.tun || data.tun->get_fd() < 0))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (data.stop.load() || !data.tun)
        return;

    auto tun = data.tun;
    const std::string &tun_name = tun->get_name();
    const bool is_bs = data.ip_addr.ends_with(".1");

    std::vector<float> llr;
    std::vector<uint8_t> frame;
    std::unordered_map<uint16_t, ReassemblyBuffer> assembly_map;

    const auto mtu = calculate_mtu(data);
    size_t mother_bits = ((sizeof(FrameHeader) + mtu) * 8 + m) * n;
    size_t mother_bytes = (mother_bits + 7) / 8;
    std::vector<uint8_t> dummy(mother_bytes * 8, 0);
    const size_t EXPECTED_LLR_SIZE = puncture(dummy, data.punct_cfg).size();

    while (!data.stop.load())
    {
        data.phy_ip.read(llr, true);

        if (llr.size() < EXPECTED_LLR_SIZE)
            continue;
        llr.resize(EXPECTED_LLR_SIZE);

        auto t_start = std::chrono::steady_clock::now();
        llr = depuncture(llr, data.punct_cfg);
        llr = deinterleaving_float(llr);
        frame = viterbi_decoder_llr(llr);
        auto t_end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();

        logs::tun.trace("[RX] Frame procesed for: {} us", duration);
        if (frame.size() < sizeof(FrameHeader) + 2)
            continue;

        FrameHeader hdr;
        memcpy(&hdr, frame.data(), sizeof(FrameHeader));
        if (ntohs(hdr.magic) != FRAME_MAGIC)
        {
            logs::tun.debug("[{}] Bad magic: 0x{:04X}", tun_name, ntohs(hdr.magic));
            continue;
        }

        auto type = static_cast<FrameType>(hdr.type);
        uint16_t src_id = ntohs(hdr.src_id);
        uint16_t dest_id = ntohs(hdr.dest_id);
        uint16_t payload_len = ntohs(hdr.length);

        if (frame.size() < sizeof(FrameHeader) + payload_len)
            continue;

        uint8_t *payload_ptr = frame.data() + sizeof(FrameHeader);

        if (is_bs && type == FrameType::JOIN)
        {
            if (payload_len < sizeof(JoinPayload) + 2)
                continue;

            std::vector<uint8_t> p_bytes(payload_ptr, payload_ptr + payload_len);
            auto rx_crc = std::vector<uint8_t>(p_bytes.end() - 2, p_bytes.end());
            p_bytes.erase(p_bytes.end() - 2, p_bytes.end());

            if (calculateCRC16(p_bytes) != rx_crc)
            {
                logs::tun.debug("[BS] JOIN CRC mismatch, dropping corrupt packet");
                continue;
            }

            JoinPayload join_req;
            memcpy(&join_req, p_bytes.data(), sizeof(JoinPayload));

            uint16_t assigned_id = 0;
            uint32_t assigned_ip = 0;

            {
                std::scoped_lock lock(g_state_mtx);
                for (auto &[cid, sess] : g_clients_by_id)
                {
                    if (sess.user_token == join_req.user_token)
                    {
                        assigned_id = sess.user_id;
                        assigned_ip = sess.ip;
                        sess.last_seen = std::chrono::steady_clock::now();
                        break;
                    }
                }

                if (assigned_id == 0)
                {
                    assigned_id = g_next_client_id++;
                    assigned_ip = htonl(0x0A000000 | (assigned_id + 1));
                    g_clients_by_id[assigned_id] = {
                        assigned_id,
                        join_req.user_token,
                        assigned_ip,
                        std::chrono::steady_clock::now()
                    };
                    g_id_by_ip[assigned_ip] = assigned_id;
                }
            }

            logs::tun.info(
                "[BS] Valid JOIN! Token: 0x{:08X} -> ID={}, IP=10.0.0.{}",
                join_req.user_token, assigned_id, (assigned_id + 1)
            );

            JoinAckPayload ack_payload{};
            ack_payload.user_token = join_req.user_token;
            ack_payload.assigned_id = htons(assigned_id);
            ack_payload.assigned_ip = assigned_ip;
            ack_payload.netmask = inet_addr("255.255.255.0");
            ack_payload.gateway = inet_addr("10.0.0.1");

            std::vector<uint8_t> ack_bytes(
                reinterpret_cast<uint8_t *>(&ack_payload),
                reinterpret_cast<uint8_t *>(&ack_payload) + sizeof(JoinAckPayload)
            );
            auto ack_crc = calculateCRC16(ack_bytes);
            ack_bytes.insert(ack_bytes.end(), ack_crc.begin(), ack_crc.end());

            FrameHeader ack_hdr{};
            ack_hdr.magic = htons(FRAME_MAGIC);
            ack_hdr.type = static_cast<uint8_t>(FrameType::JOIN_ACK);
            ack_hdr.src_id = htons(BS_USER_ID);
            ack_hdr.dest_id = htons(UNASSIGNED_ID);
            ack_hdr.length = htons(static_cast<uint16_t>(ack_bytes.size()));

            std::vector<uint8_t> ack_frame(sizeof(FrameHeader) + ack_bytes.size());
            memcpy(ack_frame.data(), &ack_hdr, sizeof(FrameHeader));
            memcpy(ack_frame.data() + sizeof(FrameHeader), ack_bytes.data(), ack_bytes.size());

            enqueue_ctrl_frame(std::move(ack_frame));
            continue;
        }

        if (!is_bs && type == FrameType::JOIN_ACK)
        {
            if (payload_len < sizeof(JoinAckPayload) + 2)
                continue;

            std::vector<uint8_t> ack_bytes(payload_ptr, payload_ptr + payload_len);
            auto rx_crc = std::vector<uint8_t>(ack_bytes.end() - 2, ack_bytes.end());
            ack_bytes.erase(ack_bytes.end() - 2, ack_bytes.end());

            if (calculateCRC16(ack_bytes) != rx_crc)
            {
                logs::tun.debug("[UE] JOIN_ACK CRC mismatch, dropping");
                continue;
            }

            JoinAckPayload ack;
            memcpy(&ack, ack_bytes.data(), sizeof(JoinAckPayload));

            if (ack.user_token == g_ue_token && !g_ue_registered.load())
            {
                g_ue_my_id = ntohs(ack.assigned_id);

                in_addr ip_addr_struct{ ack.assigned_ip };
                in_addr mask_addr_struct{ ack.netmask };
                std::string ip_str = inet_ntoa(ip_addr_struct);
                std::string mask_str = inet_ntoa(mask_addr_struct);

                logs::tun.info("[UE] Successfully registered! ID: {}, IP: {}/{}", g_ue_my_id.load(), ip_str, mask_str);

                tun->set_ip_and_up(ip_str, mask_str);
                enable_client(tun_name);

                g_ue_registered.store(true);
            }
            continue;
        }

        if (type == FrameType::DATA)
        {
            uint16_t my_id = is_bs ? BS_USER_ID : g_ue_my_id.load();

            if (dest_id != my_id && dest_id != BROADCAST_USER_ID)
                continue;

            uint16_t seq = ntohs(hdr.seq);
            auto &res_buf = assembly_map[src_id];

            if (hdr.flags & FLAG_FIRST)
            {
                res_buf.data.clear();
                res_buf.last_seq = seq;
                res_buf.active = true;
            }
            else
            {
                if (!res_buf.active || static_cast<uint16_t>(res_buf.last_seq + 1) != seq)
                {
                    res_buf.active = false;
                    res_buf.data.clear();
                    continue;
                }
                res_buf.last_seq = seq;
            }

            res_buf.last_update = std::chrono::steady_clock::now();
            res_buf.data.insert(res_buf.data.end(), payload_ptr, payload_ptr + payload_len);

            if (hdr.flags & FLAG_LAST)
            {
                auto &full_packet = res_buf.data;
                if (full_packet.size() > 2)
                {
                    auto crc_rx = std::vector<uint8_t>(full_packet.end() - 2, full_packet.end());
                    full_packet.erase(full_packet.end() - 2, full_packet.end());

                    if (calculateCRC16(full_packet) == crc_rx)
                    {
                        ssize_t written = tun->write_packet(full_packet.data(), full_packet.size());
                        if (written >= 0)
                        {
                            data.snap.is_packet = true;
                            data.history.push(data.snap);
                            data.snap.reset();
                        }
                    }
                    else
                    {
                        logs::tun.debug("[{}] DATA CRC mismatch, dropped", tun_name);
                    }
                }
                res_buf.active = false;
                res_buf.data.clear();
            }
        }
    }
}
