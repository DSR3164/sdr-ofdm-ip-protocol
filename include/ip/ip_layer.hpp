#pragma once

#include "common.hpp"

#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <string.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <netinet/ip.h>
#include <optional>
#include <atomic>

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
