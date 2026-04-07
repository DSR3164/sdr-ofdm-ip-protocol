#pragma once

#include "common.hpp"
#include "sockets.hpp"

#include <optional>

#pragma pack(push, 1)
struct FrameHeader
{
    uint16_t magic;
    uint16_t length;
    uint16_t seq;
    uint8_t flags;
    uint8_t reserved;
};
#pragma pack(pop)

struct IP
{
    std::atomic<bool> back_running = true;
    std::atomic<bool> ready_buf = false;
    std::atomic<ssize_t> nbytes = 0;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> raw_bits;
    std::vector<uint8_t> frame_bits;

    size_t seq = 0;
};

void run_tun_tx(SharedData &data);
void run_tun_rx(SharedData &data, int tun_fd, const char *tun_name);
int run_ip_gui_bridge(SharedData &data, socketData &socket);

int allocate_tun(char *dev);
std::optional<std::string> set_interface_ip(const char *dev_name);

std::vector<uint8_t> byte_to_bits(uint8_t *bytes, struct IP &sd);
