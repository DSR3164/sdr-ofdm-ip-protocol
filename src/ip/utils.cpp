#include "ip/ip_layer.hpp"
#include "ip/utils.hpp"
#include "phy/dsp_common.hpp"

size_t calculate_mtu(const SharedData &data)
{
    CarrierGrid grid(data.dsp.ofdm_cfg);
    const size_t data_subs = grid.data.size();
    const auto mod_type = data.dsp.ofdm_cfg.mod;
    const size_t bps = get_bits_per_symbol(mod_type);

    constexpr size_t num_ofdm_symbols = 10;
    const size_t bits_per_frame = data_subs * bps * num_ofdm_symbols;

    const size_t raw_payload_bits = (bits_per_frame * data.punct_cfg.period) / (n * data.punct_cfg.kept);
    const size_t raw_payload_bytes = raw_payload_bits / 8;

    const size_t overhead_bytes = sizeof(FrameHeader) + 4;
    if (raw_payload_bytes <= overhead_bytes)
        return 0;

    int result = static_cast<int>(raw_payload_bytes - overhead_bytes);

    int aligned = result & ~7;
    if (aligned < 16 && result >= 16)
        aligned = result;

    logs::tun.debug("MTU debug: data_subs={}, ps={}, mod={}, mtu={}", data_subs, grid.spacing, static_cast<int>(mod_type), aligned);

    return static_cast<size_t>(std::max(0, aligned));
}

void send_phy_frame(SharedData &data, std::vector<uint8_t> &frame, size_t max_frame_bytes)
{
    if (frame.size() < max_frame_bytes)
        frame.resize(max_frame_bytes, 0);

    auto encoded = conv_encoder(frame);
    auto bits = byte_to_bits(encoded, 8);
    bits = interleaving_(bits);
    bits = puncture(bits, data.punct_cfg);

    data.ip_phy.write(bits, true);
}
