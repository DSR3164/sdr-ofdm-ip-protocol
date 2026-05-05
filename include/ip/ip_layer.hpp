#pragma once

#include "common.hpp"
#include "sockets.hpp"

struct __attribute__((packed)) FrameHeader
{
    uint16_t magic;
    uint16_t length;
    uint16_t seq;
    uint16_t id;
    uint8_t flags;
};

constexpr uint8_t FLAG_LAST = 0x01;
constexpr uint8_t FLAG_FIRST = 0x02;

struct IP
{
    std::atomic<bool> back_running = true;
    std::atomic<bool> ready_buf = false;
    std::atomic<ssize_t> nbytes = 0;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> raw_bits;
    std::vector<uint8_t> frame_bits;

    size_t seq = 0;
    size_t id = 0;
};

struct ReassemblyBuffer
{
    std::vector<uint8_t> data;
    uint16_t last_seq = 0;
};

void run_tun_tx(SharedData &data);
void run_tun_rx(SharedData &data, int tun_fd, const char *tun_name);
int run_ip_gui_bridge(SharedData &data, socketData &socket);

std::vector<uint8_t> byte_to_bits(const std::vector<uint8_t> &bytes, int16_t r);
std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t> &bits, int16_t r);

constexpr ssize_t BUF_MTU = 250;