#pragma once

#include "common.hpp"
#include "logger.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

inline void demodulate(Modulation mod, const std::vector<std::complex<float>> &symbols, std::vector<uint8_t> &bits, std::vector<float> &llr)
{
    switch (mod)
    {
    case Modulation::BPSK: {
        llr.resize(symbols.size());
        const float scale = std::sqrt(2.0f);

        for (size_t i = 0; i < symbols.size(); ++i)
        {
            const auto sr = symbols[i].real() * scale;
            const auto si = symbols[i].imag() * scale;

            llr[i] = sr + si;
        }
        break;
    }
    case Modulation::QPSK: {
        llr.resize(symbols.size() * 2);
        const float scale = std::sqrt(2.0f);

        for (size_t i = 0; i < symbols.size(); ++i)
        {
            const auto sr = symbols[i].real() * scale;
            const auto si = symbols[i].imag() * scale;
            const size_t shift = i * 2;

            llr[shift + 0] = sr;
            llr[shift + 1] = si;
        }
        break;
    }
    case Modulation::QAM16: {
        llr.resize(symbols.size() * 4);
        const float scale = std::sqrt(10.0f);

        for (size_t i = 0; i < symbols.size(); ++i)
        {
            const auto sr = symbols[i].real() * scale;
            const auto si = symbols[i].imag() * scale;
            const size_t shift = i * 4;

            llr[shift + 0] = sr;
            llr[shift + 1] = si;

            llr[shift + 2] = 2.0f - std::abs(sr);
            llr[shift + 3] = 2.0f - std::abs(si);
        }
        break;
    }
    case Modulation::QAM64: {
        llr.resize(symbols.size() * 6);

        const float scale = std::sqrt(42.0f);

        for (size_t i = 0; i < symbols.size(); ++i)
        {
            const auto sr = symbols[i].real() * scale;
            const auto si = symbols[i].imag() * scale;
            const auto l1r = std::abs(sr);
            const auto l1i = std::abs(si);
            const size_t shift = i * 6;

            llr[shift + 0] = sr;
            llr[shift + 1] = si;

            llr[shift + 2] = 4.0f - l1r;
            llr[shift + 3] = 4.0f - l1i;

            llr[shift + 4] = 2.0f - std::abs(llr[shift + 2]);
            llr[shift + 5] = 2.0f - std::abs(llr[shift + 3]);
        }
        break;
    }
    case Modulation::QAM256: {
        llr.resize(symbols.size() * 8);

        const float scale = std::sqrt(170.0f);

        for (size_t i = 0; i < symbols.size(); ++i)
        {
            const auto sr = symbols[i].real() * scale;
            const auto si = symbols[i].imag() * scale;
            const auto l1r = std::abs(sr);
            const auto l1i = std::abs(si);
            const size_t shift = i * 8;

            llr[shift + 0] = sr;
            llr[shift + 1] = si;

            llr[shift + 2] = 8.0f - l1r;
            llr[shift + 3] = 8.0f - l1i;

            llr[shift + 4] = 4.0f - std::abs(llr[shift + 2]);
            llr[shift + 5] = 4.0f - std::abs(llr[shift + 3]);

            llr[shift + 6] = 2.0f - std::abs(llr[shift + 4]);
            llr[shift + 7] = 2.0f - std::abs(llr[shift + 5]);
        }
        break;
    }
    default:
        logs::dsp.warn("Неподдерживаемый тип демодуляции");
    }
    bits.resize(llr.size());
    for (size_t i = 0; i < llr.size(); ++i)
        bits[i] = llr[i] < 0.0f ? 1 : 0;
}
