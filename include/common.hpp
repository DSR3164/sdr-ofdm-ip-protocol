#pragma once

#include <vector>
#include <cmath>
#include <atomic>
#include <cstdint>

template <typename T>
class DoubleBuffer {
    public:
    DoubleBuffer(size_t reserve_size = 4096)
    {
        buff[0].resize(reserve_size);
        buff[1].resize(reserve_size);
    }
    ~DoubleBuffer() = default;

    int read(std::vector<T> &buffer)
    {
        if (!ready.load(std::memory_order_acquire))
            return -1;
        else
        {
            int ri = read_index.load(std::memory_order_relaxed);
            buffer = buff[ri];
            ready.store(false, std::memory_order_relaxed);
            return 0;
        }
    }
    int write(std::vector<T> &buffer)
    {
        int wi = write_index.load(std::memory_order_relaxed);
        buff[wi] = buffer;
        read_index.store(wi, std::memory_order_relaxed);
        write_index.store(wi ^ 1, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        return 0;
    }
    std::vector<T> &get_write_buffer()
    {
        int index = write_index.load(std::memory_order_relaxed);
        return buff[index];
    }
    int swap()
    {
        int wi = write_index.load(std::memory_order_relaxed);
        read_index.store(wi, std::memory_order_relaxed);
        write_index.store(wi ^ 1, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        return 0;
    }
    bool is_ready() const
    {
        return ready.load(std::memory_order_relaxed);
    }
    private:
    std::vector<T> buff[2];
    std::atomic<int> write_index{ 0 };
    std::atomic<int> read_index{ 1 };
    std::atomic<bool> ready{ false };
};

struct SharedData
{
    DoubleBuffer<uint8_t> ip_phy;

    struct IP {
        std::atomic<bool> back_running = true;
        std::atomic<bool> ready_buf = false;
        std::atomic<ssize_t> nbytes = 0;
        std::vector<int16_t> buffer;
        std::vector<int16_t> raw_bits;
    } ip;

    struct DSP {
        float cfo = 0.0f;
        int max_index = 0;
    } dsp;

    struct OFDMConfig {
        int mod = 2;
        int n_subcarriers = 128;
        int pilot_spacing = 6;
        int n_cp = 32;
        bool cfo;
    } ofdm_cfg;

    SharedData()
    {
    }

};
