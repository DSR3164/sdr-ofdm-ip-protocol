#pragma once

#include "phy/dsp_common.hpp"
#include "phy/dsp_tx/modulator.hpp"

#include <span>
#include <vector>

inline void append_symbol(const std::vector<std::complex<float>> &symbol, size_t cp, std::vector<int16_t> &buffer)
{
    for (size_t i = symbol.size() - cp; i < symbol.size(); ++i)
    {
        buffer.push_back(clip(symbol[i].real()));
        buffer.push_back(clip(symbol[i].imag()));
    }
    for (const auto &sample : symbol)
    {
        buffer.push_back(clip(sample.real()));
        buffer.push_back(clip(sample.imag()));
    }
}

class OFDMTransmitter {
    CarrierGrid grid_;
    FFTWPlan ifft_;
    std::vector<std::complex<float>> zadoff_chu_;
    size_t header_coded_bits_;
    size_t header_symbols_;

    void append_ofdm_symbol(std::span<const std::complex<float>> symbols, Modulation mod, std::vector<int16_t> &buffer);
  public:
    explicit OFDMTransmitter(const DSP::OFDMConfig &config);

    bool matches(const DSP::OFDMConfig &config) const;

    void ofdm(const std::vector<uint8_t> &input, Modulation mod, std::vector<int16_t> &buffer);
};
