#pragma once

#include "common.hpp"
#include "phy/dsp_common.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <stdexcept>
#include <vector>

inline void bpsk_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<float>(
                         bits[i] * -2.0f + 1.0f,
                         bits[i] * -2.0f + 1.0f
                     )
                     / std::sqrt(2.0f);
}

inline void qpsk_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<float>(
                         bits[2 * i + 0] * -2.0f + 1.0f,
                         bits[2 * i + 1] * -2.0f + 1.0f
                     )
                     / std::sqrt(2.0f);
}

inline void qam16_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<float>(
                         (1 - 2 * bits[4 * i + 0]) * (2 - (1 - 2 * bits[4 * i + 2])),
                         (1 - 2 * bits[4 * i + 1]) * (2 - (1 - 2 * bits[4 * i + 3]))
                     )
                     / std::sqrt(10.0f);
}

inline void qam64_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<float>(
                         (1 - 2 * bits[6 * i + 0]) * (4 - (1 - 2 * bits[6 * i + 2]) * (2 - (1 - 2 * bits[6 * i + 4]))),
                         (1 - 2 * bits[6 * i + 1]) * (4 - (1 - 2 * bits[6 * i + 3]) * (2 - (1 - 2 * bits[6 * i + 5])))
                     )
                     / std::sqrt(42.0f);
}

inline void modulate(Modulation mod, const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
{
    const size_t bps = get_bits_per_symbol(mod);
    if (bps == 0 || bits.size() % bps != 0)
        throw std::invalid_argument("Invalid modulation input");
    symbols.resize(bits.size() / bps);
    switch (mod)
    {
    case Modulation::BPSK:
        bpsk_mapper_3gpp(bits, symbols);
        break;
    case Modulation::QPSK:
        qpsk_mapper_3gpp(bits, symbols);
        break;
    case Modulation::QAM16:
        qam16_mapper_3gpp(bits, symbols);
        break;
    case Modulation::QAM64:
        qam64_mapper_3gpp(bits, symbols);
        break;
    }
}
