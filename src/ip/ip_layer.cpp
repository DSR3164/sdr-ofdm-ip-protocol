#include "sockets.hpp"
#include "ip/ip_layer.hpp"

#include <linux/if.h>
#include <netinet/in.h>

void run_tun_tx(SharedData &data)
{
    char tun_name[IFNAMSIZ] = "";
    int tun_fd = allocate_tun(tun_name);

    if (tun_fd < 0)
        return;

    auto ip_addr = set_interface_ip(tun_name);

    if (ip_addr)
    {
        logs::tun.info("Interface {} started with IP {}", tun_name, *ip_addr);
    }
    else
    {
        logs::tun.error("Failed to assign IP to {}", tun_name);
    }


    std::thread rx_thread(run_tun_rx, std::ref(data), tun_fd, tun_name);

    struct IP ip;

    uint8_t buffer[1500];
    std::vector<uint8_t> frame;
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
            frame.reserve(sizeof(FrameHeader) + nbytes);

            logs::tun.info("[{}] Read {} bytes", tun_name, static_cast<size_t>(nbytes));
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
    logs::tun.info("TUN FD {} Closed", tun_fd);

    if (rx_thread.joinable()) {
        logs::tun.info("Waiting for TX thread to join...");
        rx_thread.join();
    }
    logs::tun.info("TUN RX and TX threads finished");
}

void run_tun_rx(SharedData &data, int tun_fd, const char *tun_name)
{
    std::vector<uint8_t> frame;

    while (!data.stop.load())
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
            // logs::tun.warn("[{}] Bad magic: {:04x}", tun_name, ntohs(hdr.magic));
            continue;
        }

        uint16_t len = ntohs(hdr.length);

        if (sizeof(FrameHeader) + len > frame.size())
        {
            logs::tun.warn("[{}] Frame too short", tun_name);
            continue;
        }

        const uint8_t *payload = frame.data() + sizeof(FrameHeader);

        ssize_t written = write(tun_fd, payload, len);
        if (written < 0)
            logs::tun.error("[{}] Write error: {}", tun_name, strerror(errno));
        else
            logs::tun.info("[{}] Injected {} bytes into TUN", tun_name, written);
    }
}

int run_ip_gui_bridge(SharedData &data, socketData &socket)
{
    static IPC server;
    logs::tun.info("GUI bridge thread initialized");

    server.start_server(socket.ip_socket);

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
