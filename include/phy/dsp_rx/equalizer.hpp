#pragma once

#include "common.hpp"
#include "phy/dsp_common.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

inline void ofdm_equalize(const std::vector<std::complex<float>> &input, std::vector<std::complex<float>> &output, const CarrierGrid &grid, Modulation mod, std::vector<std::complex<float>> &channel)
{
    if (input.size() != grid.n)
        throw std::invalid_argument("Equalizer input has the wrong FFT size");

    channel.assign(grid.n, std::complex<float>{});
    const std::complex<float> pilot = get_pilot(mod);

    float maximum_power = 0.0f;
    for (const size_t k : grid.pilots)
    {
        channel[k] = input[k] / pilot;
        maximum_power = std::max(maximum_power, std::norm(channel[k]));
    }
    for (size_t p = 0; p + 1 < grid.pilots.size(); ++p)
    {
        const size_t k1 = grid.pilots[p];
        const size_t k2 = grid.pilots[p + 1];
        if (k1 <= grid.right_zone_end() && k2 >= grid.left_zone_start())
            continue;

        const float phase1 = std::arg(channel[k1]);
        const float delta_phase = std::remainder(std::arg(channel[k2]) - phase1, tau);

        const float magnitude1 = std::abs(channel[k1]);
        const float delta_magnitude = std::abs(channel[k2]) - magnitude1;

        for (size_t k = k1 + 1; k < k2; ++k)
        {
            const float alpha = static_cast<float>(k - k1) / static_cast<float>(k2 - k1);
            channel[k] = std::polar(magnitude1 + alpha * delta_magnitude, phase1 + alpha * delta_phase);
        }
    }

    const float minimum_power = std::max(1e-20f, maximum_power * 1e-8f);
    output.clear();
    output.reserve(grid.data.size());

    for (const size_t k : grid.data)
    {
        std::complex<float> value{};
        if (std::norm(channel[k]) > minimum_power)
            value = input[k] / channel[k];
        output.push_back(value);
    }
}
