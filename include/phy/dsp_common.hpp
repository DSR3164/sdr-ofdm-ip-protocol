#pragma once

#include "common.hpp"
#include "logger.hpp"
#include "ip/utils.hpp"
#include "ip/fec_codec.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fftw3.h>
#include <limits>
#include <mutex>
#include <numbers>
#include <numeric>
#include <span>
#include <vector>

constexpr auto pi = std::numbers::pi_v<float>;
constexpr auto tau = std::numbers::pi_v<float> * 2.0f;

constexpr size_t header_raw_bits = 32;
constexpr size_t max_data_symbols = 15;
constexpr size_t max_payload_bits = std::numeric_limits<uint16_t>::max();

[[nodiscard]] constexpr size_t get_bits_per_symbol(Modulation mod) noexcept
{
    switch (mod)
    {
    case Modulation::BPSK:
        return 1;
    case Modulation::QPSK:
        return 2;
    case Modulation::QAM16:
        return 4;
    case Modulation::QAM64:
        return 6;
    }
    return 0;
}

[[nodiscard]] constexpr std::complex<float> get_pilot(Modulation mod) noexcept
{
    switch (mod)
    {
    case Modulation::BPSK:
        [[fallthrough]];
    case Modulation::QPSK:
        return std::complex(0.707106781f, 0.707106781f) / std::sqrt(2.0f);
    case Modulation::QAM16:
        return std::complex(0.948683260f, 0.948683260f) / std::sqrt(2.0f);
    case Modulation::QAM64:
        return std::complex(1.080123500f, 1.080123500f) / std::sqrt(2.0f);
    }
    return 1.0f;
}

inline int16_t clip(float x)
{
    return static_cast<int16_t>(std::clamp(x, -16384.0f, 16384.0f));
}

inline size_t ceil_div(size_t value, size_t divisor)
{
    return value / divisor + static_cast<size_t>(value % divisor != 0);
}

std::mutex &fftw_mutex();

struct FFTWPlan {
    size_t size;
    fftwf_complex *in = nullptr;
    fftwf_complex *out = nullptr;
    fftwf_plan plan = nullptr;

    FFTWPlan(int size, bool direction = true);
    ~FFTWPlan();

    FFTWPlan(const FFTWPlan &) = delete;
    FFTWPlan &operator=(const FFTWPlan &) = delete;
    FFTWPlan(FFTWPlan &&) = delete;
    FFTWPlan &operator=(FFTWPlan &&) = delete;

    void clear();
};

struct CarrierGrid {
    size_t n;
    size_t cp;
    size_t spacing;
    size_t num_rbs;
    std::vector<size_t> pilots;
    std::vector<size_t> data;
    std::vector<bool> is_guard;

    explicit CarrierGrid(const DSP::OFDMConfig &config);

    size_t symbol_length() const { return n + cp; }
    size_t active_count() const { return num_rbs * 12; }
    size_t right_zone_end() const { return num_rbs * 6; }
    size_t left_zone_start() const { return n - (num_rbs * 6); }
};

void scramble(std::vector<uint8_t> &bits, uint8_t seed = 0x5B);
void descramble(std::vector<uint8_t> &bits, uint8_t seed = 0x5B);
void descramble_llr(std::vector<float> &llr_values, uint8_t seed = 0x5B);

struct DSPFrameHeader {
    Modulation modulation = Modulation::QAM64;
    uint8_t ofdm_symbols_count = 0; // Default ofdm symbols count
    uint16_t bits_count = 0;        // Max bits per frame
};

std::vector<uint8_t> packFrameHeader(const DSPFrameHeader &header);
bool unpackFrameHeader(const std::vector<uint8_t> &bits, const CarrierGrid &grid, DSPFrameHeader &header);

std::vector<uint8_t> encode(const std::vector<uint8_t> &data);
size_t encoded_header_length();
bool decode(std::vector<float> &llr, size_t encoded_size, std::vector<uint8_t> &bits);

std::vector<std::complex<float>> generate_zc(int L, int q);
std::vector<std::complex<float>> scaled_ifft_output(const FFTWPlan &ifft, size_t active_count);
std::vector<std::complex<float>> ofdm_zadoff_chu_symbol(const CarrierGrid &grid, FFTWPlan &ifft);
std::vector<std::complex<float>> ofdm_zadoff_chu_symbol(const CarrierGrid &grid);
