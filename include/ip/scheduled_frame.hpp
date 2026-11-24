#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

struct ScheduledFrame {
    uint16_t rnti;
    std::vector<uint8_t> payload;
    std::chrono::steady_clock::time_point enqueued{};
};
