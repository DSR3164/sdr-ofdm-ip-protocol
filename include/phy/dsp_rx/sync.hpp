#pragma once

#include "phy/dsp_common.hpp"

#include <cmath>
#include <complex>
#include <span>
#include <vector>

inline double cfo_est(std::span<const std::complex<float>> signal, size_t zc_start, size_t following_symbols, const CarrierGrid &grid, double fallback)
{
    std::complex<float> correlation = 0.0f;
    double total_energy = 0.0;
    auto accumulate = [&](size_t start)
    {
        if (start + grid.symbol_length() > signal.size())
            return;
        for (size_t i = 0; i < grid.cp; ++i)
        {
            const std::complex<float> prefix(signal[start + i]);
            const std::complex<float> tail(signal[start + i + grid.n]);
            correlation += std::conj(prefix) * tail;
            total_energy += 0.5 * (std::norm(prefix) + std::norm(tail));
        }
    };
    if (zc_start >= grid.cp)
        accumulate(zc_start - grid.cp);
    for (size_t s = 0; s < following_symbols; ++s)
        accumulate(zc_start + grid.n + s * grid.symbol_length());
    if (total_energy <= 1e-12 || std::abs(correlation) < 0.15 * total_energy)
        return fallback;
    return std::arg(correlation) / static_cast<double>(grid.n);
}

size_t correlation_fft_length(size_t signal_length, size_t reference_length);

class ZCCorrFFT {
  private:
    size_t reference_length_;
    size_t max_signal_length_;
    FFTWPlan forward_;
    FFTWPlan backward_;
    std::vector<std::complex<float>> reference_spectrum_;
    std::vector<double> metrics_;
    double reference_energy_ = 0.0;
  public:
    ZCCorrFFT(size_t signal_length, const std::vector<std::complex<float>> &reference);

    int find_peak(std::span<const std::complex<float>> signal, float threshold);
};
