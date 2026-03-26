#include "ip/ip_layer.hpp"

#include <spdlog/spdlog.h>
#include <linux/if.h>
#include <netinet/in.h>

void run_tun_rx(SharedData &data)
{
    char tun_name[IFNAMSIZ] = "";
    int tun_fd = allocate_tun(tun_name);

    if (tun_fd < 0)
        return;

    auto ip_addr = set_interface_ip(tun_name);

    if (ip_addr)
    {
        spdlog::info("Interface {} started with IP {}", tun_name, *ip_addr);
    }
    else
    {
        spdlog::error("Failed to assign IP to {}", tun_name);
    }


    std::thread tx_thread(run_tun_tx, std::ref(data), tun_fd, tun_name);
    tx_thread.detach();

    struct IP ip;

    uint8_t buffer[1500];
    std::vector<uint8_t> frame;
    FrameHeader hdr;

    while (ip.back_running)
    {
        ssize_t nbytes = read(tun_fd, buffer, sizeof(buffer));

        if (nbytes < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            spdlog::error("[{}] Read error {}: {}", tun_name, errno, strerror(errno));
            nbytes = 0;
            continue;
        }

        if (nbytes > 0)
        {
            ip.raw_bits.clear();
            frame.clear();
            frame.reserve(sizeof(FrameHeader) + nbytes);

            spdlog::info("[{}] Read {} bytes", tun_name, static_cast<size_t>(nbytes));
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

            ip.raw_bits = byte_to_bits(frame.data(), std::ref(ip));

            data.ip_sockets_bytes.write(frame);
            data.ip_phy.write(ip.raw_bits);
        }
    }

    close(tun_fd);
}

void run_tun_tx(SharedData &data, int tun_fd, const char *tun_name)
{
    std::vector<uint8_t> frame;

    while (true)
    {
        if (data.phy_ip.read(frame) < 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        if (frame.size() <= sizeof(FrameHeader))
            continue;

        FrameHeader hdr;
        memcpy(&hdr, frame.data(), sizeof(FrameHeader));

        if (ntohs(hdr.magic) != 0x1F35)
        {
            spdlog::warn("[{}] Bad magic: {:04x}", tun_name, ntohs(hdr.magic));
            continue;
        }

        uint16_t len = ntohs(hdr.length);

        if (sizeof(FrameHeader) + len > frame.size())
        {
            spdlog::warn("[{}] Frame too short", tun_name);
            continue;
        }

        const uint8_t *payload = frame.data() + sizeof(FrameHeader);

        ssize_t written = write(tun_fd, payload, len);
        if (written < 0)
            spdlog::error("[{}] Write error: {}", tun_name, strerror(errno));
        else
            spdlog::info("[{}] Injected {} bytes into TUN", tun_name, written);
    }
}

int run_ip_gui_bridge(SharedData &data)
{
    spdlog::info("GUI bridge thread initialized");
    static IPC server;
    static bool init = false;

    while (server.create_socket("/tmp/ip_gui.sock") == -1)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    spdlog::info("GUI bridge socket created");

    std::vector<int16_t> bits;
    std::vector<uint8_t> bytes;

    std::vector<std::byte> send_buf;

    while (1)
    {
        data.ip_sockets_bytes.read(bytes);

        if (!init)
        {
            while (server.set_socket() == -1)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            spdlog::info("GUI bridge socket initialized");
            init = true;
        }

        server.create_msg(bytes);

        if (!server.send_frame())
        {
            spdlog::warn("Client disconnected");
            init = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    spdlog::info("GUI bridge stopped");
    return 0;
}