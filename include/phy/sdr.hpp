#pragma once

#include "logger.hpp"

#include <SoapySDR/Device.hpp>
#include <cstdint>
#include <fftw3.h>
#include <vector>

enum class Flags : uint16_t
{
    None = 0,
    APPLY_GAIN = 1 << 0,
    APPLY_FREQUENCY = 1 << 1,
    APPLY_BANDWIDTH = 1 << 2,
    APPLY_SAMPLE_RATE = 1 << 3,
    REINIT = 1 << 4,
    REMODULATION = 1 << 5,
    SEND = 1 << 6,
    EXIT = 1 << 7,
    FOUND = 1 << 8,
    IS_ACTIVE = 1 << 9
};

inline Flags operator|(Flags a, Flags b)
{
    return static_cast<Flags>(
        static_cast<uint16_t>(a) | static_cast<uint16_t>(b)
    );
}

inline Flags operator&(Flags a, Flags b)
{
    return static_cast<Flags>(
        static_cast<uint16_t>(a) & static_cast<uint16_t>(b)
    );
}

inline Flags operator~(Flags a)
{
    return static_cast<Flags>(
        ~static_cast<uint16_t>(a)
    );
}

inline Flags &operator|=(Flags &a, Flags b)
{
    a = a | b;
    return a;
}

inline Flags &operator&=(Flags &a, Flags b)
{
    a = static_cast<Flags>(
        static_cast<uint16_t>(a) & static_cast<uint16_t>(b)
    );
    return a;
}

inline bool has_flag(Flags flags, Flags f)
{
    return (flags & f) != Flags::None;
}

inline bool has_any_except(Flags flags, Flags excluded)
{
    return (flags & ~excluded) != Flags::None;
}

struct SDRConfig {
    float sample_rate = 1.92e6f;
    int buffer_size = static_cast<int>(sample_rate / 1e3f);
    float tx_freq = 2200e6f; // FDD: set rx_freq to tx_freq + 3MHz on the other node
    float rx_freq = 2200e6f; // FDD: adjust per node, see Configuration in README
    float tx_gain = 89.0f;
    float rx_gain = 25.0f;
    bool enable_tx = true;
    bool enable_rx = true;
    float tx_bandwidth = 1e6f;
    float rx_bandwidth = 10e6f;
    bool init_on_start = true;
    bool exit_on_error = true;
};

class SDR {
  public:
    explicit SDR(const SDRConfig &config, std::atomic<bool> &stop_condition);
    ~SDR()
    {
        (void)deinit();
    }

    SDR(const SDR &) = delete;
    SDR &operator=(const SDR &) = delete;

    [[nodiscard]] bool init();
    [[nodiscard]] bool reinit();
    [[nodiscard]] bool deinit();
    [[nodiscard]] bool check_connection();
    void scan();
    void wait_connection();
    int add_args();
    void apply_runtime();

    int readstream(std::vector<int16_t> &send);
    int writestream(std::vector<int16_t> &send);

    void set_rx_freq(float f)
    {
        logs::sdr.info("RX carrier: {:.3f} GHz", f / 1e9f);
        cfg.rx_freq = f;
        flags |= Flags::APPLY_FREQUENCY;
    }
    void set_tx_freq(float f)
    {
        logs::sdr.info("TX carrier: {:.3f} GHz", f / 1e9f);
        cfg.tx_freq = f;
        flags |= Flags::APPLY_FREQUENCY;
    }
    void set_rx_gain(float g)
    {
        logs::sdr.info("RX gain: {:.2f} dB", g);
        cfg.rx_gain = g;
        flags |= Flags::APPLY_GAIN;
    }
    void set_tx_gain(float g)
    {
        logs::sdr.info("TX gain: {:.2f} dB", g);
        cfg.tx_gain = g;
        flags |= Flags::APPLY_GAIN;
    }

    bool get_wait_flag() const { return cfg.init_on_start; };
    bool get_exit_flag() const { return cfg.exit_on_error; };
    int get_buffer_size() const { return cfg.buffer_size; }
    float get_sample_rate() const { return cfg.sample_rate; }
    Flags get_flags() const { return flags; }
    bool add_flag(Flags flag)
    {
        (flags |= flag);
        return true;
    }

    static constexpr int RX = SOAPY_SDR_RX;
    static constexpr int TX = SOAPY_SDR_TX;
  private:
    std::atomic<bool> &cond;
    SDRConfig cfg;
    Flags flags = Flags::None;

    size_t channels[1] = { 0 };
    SoapySDR::Device *sdr = nullptr;
    SoapySDR::Stream *rxStream = nullptr;
    SoapySDR::Stream *txStream = nullptr;
    SoapySDR::Kwargs args;

    size_t connection_retries = 0;
    int sdr_flags = 0;
    long long timeNs = 0;
    long long timeNSdelay = 2e6;
    long timeoutUs = 0;
};
