#include "phy/dsp_tx/transmitter.hpp"

#include <algorithm>
#include <stdexcept>

void OFDMTransmitter::append_ofdm_symbol(std::span<const std::complex<float>> symbols, Modulation mod, std::vector<int16_t> &buffer)
{
    ifft_.clear();
    const auto pilot = get_pilot(mod);
    for (const size_t k : grid_.pilots)
    {
        ifft_.in[k][0] = pilot.real();
        ifft_.in[k][1] = pilot.imag();
    }
    for (size_t i = 0; i < symbols.size(); ++i)
    {
        const size_t k = grid_.data[i];
        ifft_.in[k][0] = symbols[i].real();
        ifft_.in[k][1] = symbols[i].imag();
    }
    fftwf_execute(ifft_.plan);
    append_symbol(scaled_ifft_output(ifft_, grid_.active_count()), grid_.cp, buffer);
}

OFDMTransmitter::OFDMTransmitter(const DSP::OFDMConfig &config)
    : grid_(config),
      ifft_(grid_.n, false),
      zadoff_chu_(ofdm_zadoff_chu_symbol(grid_, ifft_)),
      header_coded_bits_(encoded_header_length()),
      header_symbols_(ceil_div(header_coded_bits_, grid_.data.size()))
{
}

bool OFDMTransmitter::matches(const DSP::OFDMConfig &config) const
{
    return grid_.n == config.n_subcarriers && grid_.cp == config.n_cp && grid_.spacing == config.pilot_spacing;
}

void OFDMTransmitter::ofdm(const std::vector<uint8_t> &input, Modulation mod, std::vector<int16_t> &buffer)
{
    buffer.clear();
    const size_t bps = get_bits_per_symbol(mod);
    if (bps == 0 || input.empty() || input.size() > max_payload_bits || std::any_of(input.begin(), input.end(), [](uint8_t bit)
                                                                                    { return bit > 1; }))
        throw std::invalid_argument("Invalid payload or payload exceeds 65535 bits");
    const size_t count = ceil_div(input.size(), grid_.data.size() * bps);
    if (count > max_data_symbols)
        throw std::invalid_argument("Payload exceeds the 15-symbol PHY header limit");

    DSPFrameHeader header;
    header.modulation = mod;
    header.ofdm_symbols_count = static_cast<uint8_t>(count);
    header.bits_count = static_cast<uint16_t>(input.size());
    auto header_bits = encode(packFrameHeader(header));
    if (header_bits.size() != header_coded_bits_)
        throw std::runtime_error("Header encoder length changed");
    header_bits.resize(header_symbols_ * grid_.data.size(), 0);
    scramble(header_bits);
    std::vector<std::complex<float>> header_symbols;
    modulate(Modulation::BPSK, header_bits, header_symbols);

    auto bits = input;
    scramble(bits);
    bits.resize(ceil_div(bits.size(), bps) * bps, 0);
    std::vector<std::complex<float>> symbols;
    modulate(mod, bits, symbols);
    buffer.reserve((1 + header_symbols_ + count) * grid_.symbol_length() * 2);
    append_symbol(zadoff_chu_, grid_.cp, buffer);
    for (size_t s = 0; s < header_symbols_; ++s)
        append_ofdm_symbol(std::span<const std::complex<float>>(header_symbols).subspan(s * grid_.data.size(), grid_.data.size()), Modulation::BPSK, buffer);
    for (size_t s = 0; s < count; ++s)
    {
        const size_t start = s * grid_.data.size();
        append_ofdm_symbol(std::span<const std::complex<float>>(symbols).subspan(start, std::min(grid_.data.size(), symbols.size() - start)), mod, buffer);
    }
}
