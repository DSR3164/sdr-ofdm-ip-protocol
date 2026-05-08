#pragma once

#include "phy/sdr.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
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

inline const std::string mod_to_string(Modulation mod)
{
    switch (mod)
    {
    case Modulation::BPSK:
        return "BPSK";
    case Modulation::QPSK:
        return "QPSK";
    case Modulation::QAM16:
        return "QAM16";
    case Modulation::QAM64:
        return "QAM64";
    default:
        return "ERROR";
    }
}

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
        {
            while (!ready.load(std::memory_order_acquire) && !stopped.load(std::memory_order_acquire))
                ready.wait(false, std::memory_order_acquire);
            if (stopped.load())
                return -1;
        }
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
        {
            while (ready.load(std::memory_order_acquire) && !stopped.load(std::memory_order_acquire))
                ready.wait(true, std::memory_order_acquire);
            if (stopped.load())
                return -1;
        }
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
        {
            while (ready.load(std::memory_order_acquire) && !stopped.load(std::memory_order_acquire))
                ready.wait(true, std::memory_order_acquire);
            if (stopped.load())
                return -1;
        }
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
    void stop()
    {
        stopped.store(true, std::memory_order_seq_cst);
        ready.store(false);
        ready.notify_all();
        ready.store(true);
        ready.notify_all();
    }
  private:
    std::vector<T> buff[2];
    std::atomic<int> write_index{ 0 };
    std::atomic<int> read_index{ 1 };
    std::atomic<bool> ready{ false };
    std::atomic<bool> stopped{ false };
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

    std::string ip_addr;

    std::atomic<bool> stop{ false };

    void stop_all_buffers()
    {
        ip_phy.stop();
        phy_ip.stop();
        sdr_dsp_tx.stop();
        sdr_dsp_rx.stop();
        dsp_sockets_raw.stop();
        dsp_sockets_symbols.stop();
        ip_sockets_bytes.stop();
    }

    SharedData() : sdr(SDRConfig{}),
                   dsp_sockets_raw(SDRConfig{}.buffer_size * 2),
                   dsp_sockets_symbols(SDRConfig{}.buffer_size * 2) {}
};
