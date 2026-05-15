#include "sockets.hpp"
#include "ip/bit_utils.hpp"
#include "ip/crc.hpp"
#include "ip/fec_codec.hpp"
#include "ip/ip_layer.hpp"
#include "ip/ip_nat.hpp"
#include "ip/tun_layer.hpp"

#include <SDL2/SDL_stdinc.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <linux/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/poll.h>
#include <thread>
#include <vector>

void run_tun_tx(SharedData &data)
{
    char tun_name[IFNAMSIZ] = "";
    int tun_fd = allocate_tun(tun_name);

    if (tun_fd < 0)
        return;

    std::string ip_address = data.ip_addr;

    auto ip_addr = set_interface_ip(tun_name, ip_address);
    if (ip_addr)
    {
        logs::tun.info("Interface {} started with IP {}", tun_name, *ip_addr);

        if (ip_addr->ends_with(".1"))
            enable_nat(tun_name);
        else if (!ip_addr->ends_with(".1"))
            enable_client(tun_name);
    }
    else
        logs::tun.error("Failed to assign IP to {}", tun_name);

    std::thread rx_thread(run_tun_rx, std::ref(data), tun_fd, tun_name);

    struct IP ip;

    uint8_t buffer[1500];
    std::vector<uint8_t> frame;
    std::vector<uint32_t> encoded_bytes;
    FrameHeader hdr;

    while (!data.stop.load())
    {
        struct pollfd pfd = { tun_fd, POLLIN, 0 };
        int ret = poll(&pfd, 1, -1);

        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        if (pfd.revents & POLLIN)
        {
            ssize_t nbytes = read(tun_fd, buffer, sizeof(buffer));
            if (nbytes < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                logs::tun.error("[{}] Read error {}: {}", tun_name, errno, strerror(errno));
                nbytes = 0;
                continue;
            }

            if (nbytes > 0)
            {

                logs::tun.trace("Get packet {} bytes, id: {}", nbytes, ip.id);
                std::vector<uint8_t> payload(buffer, buffer + nbytes);

                auto crc = calculateCRC16(payload);
                payload.insert(payload.end(), crc.begin(), crc.end());

                size_t total_len = payload.size();
                size_t offset = 0;
                uint16_t packet_id = static_cast<uint16_t>(ip.id++ & 0xFFFF);
                uint16_t packet_seq = 0;

                while (offset < total_len)
                {
                    size_t chunk_size = std::min<size_t>(BUF_MTU, total_len - offset);
                    uint8_t hflag = 0;
                    if (offset == 0)
                        hflag |= FLAG_FIRST;
                    if (offset + chunk_size >= total_len)
                        hflag |= FLAG_LAST;

                    hdr.magic = htons(0x1F35);
                    hdr.length = htons(static_cast<uint16_t>(chunk_size));
                    hdr.seq = htons(packet_seq++);
                    hdr.id = htons(packet_id);
                    hdr.flags = hflag;

                    std::vector<uint8_t> frame;
                    auto *hdr_ptr = reinterpret_cast<uint8_t *>(&hdr);
                    frame.insert(frame.end(), hdr_ptr, hdr_ptr + sizeof(FrameHeader));
                    frame.insert(frame.end(), payload.begin() + offset, payload.begin() + offset + chunk_size);

                    auto encoded = hamming_encoder(frame);
                    encoded = interleaving(encoded);
                    auto bits = byte_to_bits(encoded, 32);

                    data.ip_phy.write(bits, true);
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));

                    offset += chunk_size;
                }
            }
        }
    }

    close(tun_fd);
    logs::tun.info("TUN FD {} Closed", tun_fd);

    if (rx_thread.joinable())
    {
        logs::tun.info("Waiting for RX thread to join...");
        rx_thread.join();
    }
    logs::tun.info("TUN RX and TX threads finished");
}

void run_tun_rx(SharedData &data, int tun_fd, const char *tun_name)
{
    std::vector<uint8_t> frame;
    std::vector<uint32_t> block;

    std::unordered_map<uint16_t, ReassemblyBuffer> assembly_map;
    auto last_cleanup = std::chrono::steady_clock::now();

    while (!data.stop.load())
    {
        data.phy_ip.read(frame, true);

        block = bits_to_bytes<uint32_t>(frame, 32);
        block = deinterleaving(block);
        frame = hamming_decoder(block);

        if (frame.size() < sizeof(FrameHeader) + 2)
            continue;

        FrameHeader hdr;
        memcpy(&hdr, frame.data(), sizeof(FrameHeader));
        if (ntohs(hdr.magic) != 0x1F35)
        {
            logs::tun.debug("[{}] Bad magic: 0x{:04X}", tun_name, ntohs(hdr.magic));
            continue;
        }

        uint16_t payload_len = ntohs(hdr.length);
        if (payload_len > frame.size() - sizeof(FrameHeader))
            continue;
        uint8_t *fragment_data = frame.data() + sizeof(FrameHeader);
        uint16_t current_seq = ntohs(hdr.seq);
        uint16_t id = ntohs(hdr.id);

        auto &res_buf = assembly_map[id];
        res_buf.last_update = std::chrono::steady_clock::now();

        if (hdr.flags & FLAG_FIRST)
        {
            res_buf.data.clear();
            res_buf.last_seq = current_seq;
        }

        res_buf.data.insert(res_buf.data.end(), fragment_data, fragment_data + payload_len);

        if (hdr.flags & FLAG_LAST)
        {
            std::vector<uint8_t> &full_packet = res_buf.data;

            if (full_packet.size() > 2)
            {
                std::vector<uint8_t> crc_received(full_packet.end() - 2, full_packet.end());
                full_packet.resize(full_packet.size() - 2);

                std::vector<uint8_t> crc_calc = calculateCRC16(full_packet);

                if (crc_calc[0] == crc_received[0] && crc_calc[1] == crc_received[1])
                {
                    ssize_t written = write(tun_fd, full_packet.data(), full_packet.size());

                    if (written < 0)
                        logs::tun.error("[{}] Write error: {}", tun_name, strerror(errno));
                }
                else
                {
                    std::vector<uint8_t> gui_frame;
                    gui_frame.insert(gui_frame.end(), (uint8_t *)&hdr, (uint8_t *)&hdr + sizeof(FrameHeader));
                    gui_frame.insert(gui_frame.end(), full_packet.begin(), full_packet.end());
                    data.ip_sockets_bytes.write(gui_frame);

                    logs::tun.error("[{}] CRC Failed for assembled packet ID {}", tun_name, (uint16_t)id);
                }
            }
            assembly_map.erase(id);
        }

        auto now = std::chrono::steady_clock::now();

        if (now - last_cleanup > std::chrono::seconds(1))
        {
            last_cleanup = now;
            for (auto it = assembly_map.begin(); it != assembly_map.end();)
            {
                if (now - it->second.last_update > std::chrono::seconds(2))
                {
                    logs::tun.debug("[{}] Packet ID {} timed out, dropping", tun_name, it->first);
                    it = assembly_map.erase(it);
                }
                else
                    ++it;
            }
        }
    }
}

int run_ip_gui_bridge(SharedData &data, socketData &socket)
{
    static IPC server;
    bool init = false;
    logs::tun.info("GUI bridge thread initialized");

    while (!init && !data.stop.load())
    {
        if (!server.start_server(socket.ip_socket))
        {
            logs::socket.error("Failed to start IP to GUI bridge");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        else
        {
            logs::socket.info("IP to GUI bridge server started");
            init = true;
        }
    }

    while (!data.stop.load())
    {
        std::vector<uint8_t> bytes;

        if (data.ip_sockets_bytes.read(bytes) == 0)
        {
            if (!server.send_frame(MsgType::Vector, bytes))
                logs::socket.error("Frame send failed");
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    logs::tun.info("GUI bridge stopped");
    return 0;
}
