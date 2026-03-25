#pragma once

#include "common.hpp"

struct IP
{
    std::atomic<bool> back_running = true;
    std::atomic<bool> ready_buf = false;
    std::atomic<ssize_t> nbytes = 0;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> raw_bits;
};

void run_tun_rx(SharedData &data);
int run_ip_gui_bridge(SharedData &data);

int allocate_tun(char *dev);
std::optional<std::string> set_interface_ip(const char *dev_name);

std::vector<uint8_t> byte_to_bits(uint8_t *bytes, struct IP &sd);
