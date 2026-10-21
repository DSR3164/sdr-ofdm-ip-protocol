#pragma once

#include "common.hpp"

#include <chrono>
#include <cstdint>

constexpr uint16_t FRAME_MAGIC = 0x1F35;

enum class FrameType : uint8_t
{
    JOIN = 0x01,
    JOIN_ACK = 0x02,
    JOIN_NACK = 0x03,
    DATA = 0x04,
    DISCONNECT = 0x05,
};

constexpr uint8_t FLAG_LAST = 0x01;
constexpr uint8_t FLAG_FIRST = 0x02;

constexpr uint16_t BS_USER_ID = 0x0000;
constexpr uint16_t BROADCAST_USER_ID = 0xFFFF;
constexpr uint16_t UNASSIGNED_ID = 0xFFFF;

#pragma pack(push, 1)
struct FrameHeader {
    uint16_t magic;
    uint8_t type;
    uint16_t src_id;
    uint16_t dest_id;
    uint16_t seq;
    uint16_t length;
    uint8_t flags;
};

struct JoinPayload {
    uint32_t user_token;
    uint32_t auth_key;
};

struct JoinAckPayload {
    uint32_t user_token;
    uint16_t assigned_id;
    uint32_t assigned_ip; // in network byte order
    uint32_t netmask;     // in network byte order
    uint32_t gateway;     // in network byte order
};
#pragma pack(pop)

struct ClientSession {
    uint16_t user_id;
    uint32_t user_token;
    uint32_t ip; // in network byte order
    std::chrono::steady_clock::time_point last_seen;
};

struct ReassemblyBuffer {
    std::vector<uint8_t> data;
    uint16_t last_seq = 0;
    bool active = false;
    std::chrono::steady_clock::time_point last_update;
};

struct IP {
    std::atomic<bool> back_running = true;
    std::atomic<bool> ready_buf = false;
    std::atomic<ssize_t> nbytes = 0;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> raw_bits;
    std::vector<uint8_t> frame_bits;

    size_t seq = 0;
    size_t id = 0;
};

void run_tun_tx(SharedData &data);
void run_tun_rx(SharedData &data);
