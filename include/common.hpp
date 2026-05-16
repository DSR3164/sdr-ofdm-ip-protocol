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

struct DSP {
    float cfo = 0.0f;
    int max_index = 0;
    int offset = -2;
    float sample_rate = 1.92e6;
    struct OFDMConfig {
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

struct StatsSummary {
    uint32_t zc_not_found = 0;
    uint32_t cp_not_found = 0;
    uint32_t cfo_jumped = 0;
    uint32_t packet_found = 0;
    uint32_t packet_lost = 0;
    float packet_loss = 0;
    float mean_time_us = 0.0f;
};

struct StatsSnapshot {
    int16_t cp_pos;
    int16_t zc_pos;
    bool cp_found;
    bool zc_found;
    bool is_packet;
    bool is_previous_packet_lost;
    float processing_time_us;
    float cfo;

    void reset()
    {
        *this = {};
    }
};

template <size_t N>
struct StatsHistory {
    std::atomic<size_t> zc_not_found{ 0 };
    std::atomic<size_t> cp_not_found{ 0 };
    std::atomic<size_t> packet_found{ 0 };
    std::atomic<size_t> packet_lost{ 0 };

    std::array<StatsSnapshot, N> buffer;
    std::atomic<size_t> head = 0;
    std::atomic<StatsSnapshot *> latest = nullptr;

    void push(StatsSnapshot s)
    {
        if (!s.zc_found)
            zc_not_found.fetch_add(1, std::memory_order_relaxed);
        if (!s.cp_found)
            cp_not_found.fetch_add(1, std::memory_order_relaxed);
        if (s.is_packet)
            packet_found.fetch_add(1, std::memory_order_relaxed);
        if (s.is_previous_packet_lost)
            packet_lost.fetch_add(1, std::memory_order_relaxed);

        size_t idx = head.fetch_add(1, std::memory_order_relaxed) % N;
        buffer[idx] = s;
        latest.store(&buffer[idx], std::memory_order_release);
    }

    bool get_last(StatsSnapshot &out)
    {
        auto *p = latest.load(std::memory_order_acquire);
        if (!p)
            return false;
        out = *p;
        return true;
    }

    StatsSummary get_summary() const
    {
        StatsSummary res;
        size_t current_head = head.load(std::memory_order_relaxed);
        size_t count = std::min(current_head, N);
        size_t time_count = 0;

        res.zc_not_found = zc_not_found.load(std::memory_order_relaxed);
        res.cp_not_found = cp_not_found.load(std::memory_order_relaxed);
        res.packet_found = packet_found.load(std::memory_order_relaxed);
        res.packet_lost = packet_lost.load(std::memory_order_relaxed);

        if (count == 0)
            return res;

        size_t start = (current_head > N) ? (current_head % N) : 0;
        for (size_t i = 0; i < count; ++i)
        {
            size_t idx = (start + i) % N;
            const auto &current = buffer[idx];
            res.mean_time_us += current.processing_time_us;
            if (i > 0)
            {
                size_t prev_idx = (start + i - 1) % N;
                const auto &prev = buffer[prev_idx];

                if (current.cp_found && prev.cp_found)
                    if (std::abs(current.cfo - prev.cfo) > 500.0f)
                        res.cfo_jumped++;
            }
        }
        if ((res.packet_lost + res.packet_found) != 0)
            res.packet_loss = (res.packet_lost * 100.0f) / (res.packet_lost + res.packet_found);
        res.mean_time_us /= count;

        return res;
    }
};

struct SharedData {
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

    SharedData()
        : sdr(SDRConfig{}),
          dsp_sockets_raw(SDRConfig{}.buffer_size * 2),
          dsp_sockets_symbols(SDRConfig{}.buffer_size * 2)
    {
    }
};
