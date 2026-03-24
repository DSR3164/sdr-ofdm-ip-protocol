#pragma once

#include <SoapySDR/Device.hpp>
#include <cstdint>
#include <fftw3.h>
#include <vector>
#include <iostream>

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
    IS_ACTIVE = 1 << 8,
};

inline Flags operator|(Flags a, Flags b)
{
    return static_cast<Flags>(
        static_cast<uint16_t>(a) |
        static_cast<uint16_t>(b));
}

inline Flags operator&(Flags a, Flags b)
{
    return static_cast<Flags>(
        static_cast<uint16_t>(a) &
        static_cast<uint16_t>(b));
}

inline Flags operator~(Flags a)
{
    return static_cast<Flags>(
        ~static_cast<uint16_t>(a));
}

inline Flags &operator|=(Flags &a, Flags b)
{
    a = a | b;
    return a;
}

inline Flags &operator&=(Flags &a, Flags b)
{
    a = static_cast<Flags>(
        static_cast<uint16_t>(a) &
        static_cast<uint16_t>(b));
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
    int buffer_size = 1920;
    double sample_rate = 1.92e6;
    double tx_freq = 2000e6;
    double rx_freq = 2000e6;
    float tx_gain = 89.0f;
    float rx_gain = 25.0f;
    bool enable_tx = true;
    bool enable_rx = true;
    float tx_bandwidth = 1e6;
    float rx_bandwidth = 10e6;
};

class SDR {
    public:

    explicit SDR(const SDRConfig &config);
    ~SDR() { if (deinit()) {}; }

    SDR(const SDR &) = delete;
    SDR &operator=(const SDR &) = delete;

    [[nodiscard]] bool init();
    [[nodiscard]] bool reinit();
    [[nodiscard]] bool deinit();
    int add_args();
    void apply_runtime();

    int readstream(std::vector<int16_t> &send);
    int writestream(std::vector<int16_t> &send);

    void set_rx_freq(double f) { cfg.rx_freq = f; flags |= Flags::APPLY_FREQUENCY; }
    void set_tx_freq(double f) { cfg.tx_freq = f; flags |= Flags::APPLY_FREQUENCY; }
    void set_rx_gain(float g) { cfg.rx_gain = g; flags |= Flags::APPLY_GAIN; }
    void set_tx_gain(float g) { cfg.tx_gain = g; flags |= Flags::APPLY_GAIN; }

    int get_buffer_size() const { return cfg.buffer_size; }
    double get_sample_rate() const { return cfg.sample_rate; }
    Flags get_flags() const { return flags; }
    bool add_flag(Flags flag) { (flags |= flag); return true; }

    static constexpr int RX = SOAPY_SDR_RX;
    static constexpr int TX = SOAPY_SDR_TX;

    private:
    SDRConfig cfg;
    Flags flags = Flags::None;

    size_t channels[1] = { 0 };
    SoapySDR::Device *sdr = nullptr;
    SoapySDR::Stream *rxStream = nullptr;
    SoapySDR::Stream *txStream = nullptr;
    SoapySDR::Kwargs args;

    int sdr_flags = 0;
    long long timeNs = 0;
    long timeoutUs = 0;

};
