#pragma once

#include "phy/sdr.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

extern std::atomic<bool> *stop_ptr;

enum class Modulation
{
    BPSK,
    QPSK,
    QAM16,
    QAM64,
};

struct DSP
{
    float cfo = 0.0f;
    int max_index = 0;
    int offset = -2;
    float sample_rate = 1.92e6;
    struct OFDMConfig
    {
        Modulation mod = Modulation::QAM16;
        int n_subcarriers = 128;
        int pilot_spacing = 19;
        int n_cp = 32;
    } ofdm_cfg;
};

template <typename T>
class DoubleBuffer {
  public:
    DoubleBuffer(size_t reserve_size = 1920 * 2)
    {
        buff[0].resize(reserve_size);
        buff[1].resize(reserve_size);
    }
    ~DoubleBuffer() = default;

    int read(std::vector<T> &buffer, bool blocking = false)
    {
        if (blocking)
            ready.wait(false, std::memory_order_acquire);
        else if (!ready.load(std::memory_order_acquire))
            return -1;
        int ri = read_index.load(std::memory_order_relaxed);
        buffer = buff[ri];
        ready.store(false, std::memory_order_release);
        if (blocking)
            ready.notify_one();
        return 0;
    }
    int write(std::vector<T> &buffer, bool blocking = false)
    {
        if (blocking)
            ready.wait(true, std::memory_order_acquire);
        int wi = write_index.load(std::memory_order_relaxed);
        buff[wi] = buffer;
        read_index.store(wi, std::memory_order_relaxed);
        write_index.store(wi ^ 1, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        if (blocking)
            ready.notify_one();
        return 0;
    }
    std::vector<T> &get_write_buffer()
    {
        int index = write_index.load(std::memory_order_relaxed);
        return buff[index];
    }
    int swap(bool blocking = false)
    {
        if (blocking)
            ready.wait(true, std::memory_order_acquire);
        int wi = write_index.load(std::memory_order_relaxed);
        read_index.store(wi, std::memory_order_relaxed);
        write_index.store(wi ^ 1, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        if (blocking)
            ready.notify_one();
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
    SDR sdr;
    DSP dsp;

    DoubleBuffer<uint8_t> ip_phy;
    DoubleBuffer<uint8_t> phy_ip;

    DoubleBuffer<int16_t> sdr_dsp_tx;
    DoubleBuffer<int16_t> sdr_dsp_rx;

    DoubleBuffer<std::complex<float>> dsp_sockets_raw;
    DoubleBuffer<std::complex<float>> dsp_sockets_symbols;
    DoubleBuffer<uint8_t> ip_sockets_bytes;

    std::atomic<bool> stop{ false };

    SharedData() : sdr(SDRConfig{}),
                   dsp_sockets_raw(SDRConfig{}.buffer_size * 2),
                   dsp_sockets_symbols(SDRConfig{}.buffer_size * 2) {}
};
