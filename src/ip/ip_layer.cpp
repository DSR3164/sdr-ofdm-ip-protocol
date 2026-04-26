#include "sockets.hpp"
#include "ip/bit_utils.hpp"
#include "ip/crc.hpp"
#include "ip/fec_codec.hpp"
#include "ip/ip_layer.hpp"
#include "ip/tun_layer.hpp"

#include <SDL2/SDL_stdinc.h>
#include <chrono>
#include <cstdint>
#include <linux/if.h>
#include <netinet/in.h>
#include <thread>
#include <vector>

void run_tun_tx(SharedData &data)
{
    char tun_name[IFNAMSIZ] = "";
    int tun_fd = allocate_tun(tun_name);

    if (tun_fd < 0)
        return;

    uint8_t node_id = node_id_prompt();

    auto ip_addr = set_interface_ip(tun_name, node_id);

    if (ip_addr)
        logs::tun.info("Interface {} started with IP {}", tun_name, *ip_addr);
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
            ip.raw_bits.clear();
            frame.clear();
            frame.reserve(sizeof(FrameHeader) + nbytes + 2);

            logs::tun.trace("[{}] Read {} bytes", tun_name, static_cast<size_t>(nbytes));
            ip.nbytes = nbytes;

            ip.buffer.assign(buffer, buffer + nbytes);
            ip.ready_buf.store(true, std::memory_order_release);

            hdr.magic = htons(0x1F35);
            hdr.length = htons(nbytes);
            hdr.seq = htons(ip.seq++);
            hdr.flags = 0;
            hdr.reserved = 0;

            auto *hdr_ptr = reinterpret_cast<uint8_t *>(&hdr);
            frame.insert(frame.end(), hdr_ptr, hdr_ptr + sizeof(FrameHeader));
            frame.insert(frame.end(), buffer, buffer + nbytes);

            auto crc = calculateCRC16(frame);
            frame.insert(frame.end(), crc.begin(), crc.end());

            encoded_bytes = hamming_encoder(frame);
            encoded_bytes = interleaving(encoded_bytes);

            ip.raw_bits = byte_to_bits(encoded_bytes, 32);

            data.ip_phy.write(ip.raw_bits, true);
        }
    }

    close(tun_fd);
    logs::tun.info("TUN FD {} Closed", tun_fd);

    if (rx_thread.joinable())
    {
        logs::tun.info("Waiting for TX thread to join...");
        rx_thread.join();
    }
    logs::tun.info("TUN RX and TX threads finished");
}

void run_tun_rx(SharedData &data, int tun_fd, const char *tun_name)
{
    std::vector<uint8_t> frame;
    std::vector<uint32_t> block;

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
        uint16_t payload_len = ntohs(hdr.length);
        size_t expected_len = sizeof(FrameHeader) + payload_len + 2;

        frame.resize(expected_len);

        if (ntohs(hdr.magic) != 0x1F35)
        {
            logs::tun.debug("[{}] Bad magic: 0x{:04X}", tun_name, ntohs(hdr.magic));
            continue;
        }

        std::vector<uint8_t> crc_received(frame.end() - 2, frame.end());

        frame.resize(sizeof(FrameHeader) + payload_len);

        std::vector<uint8_t> crc_calc = calculateCRC16(frame);

        if (crc_calc[0] != crc_received[0] || crc_calc[1] != crc_received[1])
        {
            logs::tun.error("[{}] CRC mismatch: received {:02X}{:02X}, calculated {:02X}{:02X} | payload_len={}", tun_name, crc_received[0], crc_received[1], crc_calc[0], crc_calc[1], payload_len);
            continue;
        }

        const uint8_t *payload = frame.data() + sizeof(FrameHeader);
        ssize_t written = write(tun_fd, payload, payload_len);

        std::vector<uint8_t> payload_vec(frame.data(), frame.data() + sizeof(FrameHeader) + payload_len);

        data.ip_sockets_bytes.write(payload_vec);

        if (written < 0)
            logs::tun.error("[{}] Write error: {}", tun_name, strerror(errno));
        else
            logs::tun.trace("[{}] Injected {} bytes into TUN", tun_name, written);
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
