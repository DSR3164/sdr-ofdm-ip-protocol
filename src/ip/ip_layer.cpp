#include "ip/ip_layer.hpp"
#include <spdlog/spdlog.h>

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

    struct IP ip;

    uint8_t buffer[1500];

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
            spdlog::info("[{}] Read {} bytes", tun_name, static_cast<size_t>(nbytes));
            ip.nbytes = nbytes;

            ip.raw_bits = byte_to_bits(buffer, std::ref(ip));
            ip.buffer.assign(buffer, buffer + nbytes);
            ip.ready_buf.store(true, std::memory_order_release);

            data.ip_sockets_bytes.write(ip.buffer);
            // data.ip_phy.write(ip.raw_bits);
        }
    }

    close(tun_fd);
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