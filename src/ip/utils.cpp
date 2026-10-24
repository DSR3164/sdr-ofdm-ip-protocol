#include "ip/ip_layer.hpp"
#include "ip/utils.hpp"

size_t calculate_mtu(const SharedData &data)
{
    const size_t ns = data.dsp.ofdm_cfg.n_subcarriers;
    const int ps = data.dsp.ofdm_cfg.pilot_spacing;
    const auto mod_type = data.dsp.ofdm_cfg.mod;

    size_t data_subs = 0;
    size_t active_idx = 0;

    const size_t guard_left = ns / 8;
    const size_t guard_right = ns / 8;

    for (size_t k = 0; k < ns; ++k)
    {
        if (k == 0 || k < guard_left || k >= (ns - guard_right))
            continue;

        bool is_pilot = (active_idx % ps == 0) || (k == ns - guard_right - 1);
        if (!is_pilot)
            data_subs++;

        active_idx++;
    }

    uint16_t bps = 1;
    switch (mod_type)
    {
    case Modulation::BPSK:
        bps = 1;
        break;
    case Modulation::QPSK:
        bps = 2;
        break;
    case Modulation::QAM16:
        bps = 4;
        break;
    case Modulation::QAM64:
        bps = 6;
        break;
    default:
        bps = 1;
        break;
    }

    constexpr size_t num_ofdm_symbols = 10;
    const size_t bits_per_frame = data_subs * bps * num_ofdm_symbols;

    const size_t raw_payload_bits = (bits_per_frame * data.punct_cfg.period) / (n * data.punct_cfg.kept);
    const size_t raw_payload_bytes = raw_payload_bits / 8;

    if (raw_payload_bytes <= sizeof(FrameHeader) + 4)
        return 0;

    int result = static_cast<int>(raw_payload_bytes - sizeof(FrameHeader) - 4);

    logs::tun.debug("MTU debug: data_subs={}, ps={}, mod={}", data_subs, ps, static_cast<int>(mod_type));

    return static_cast<size_t>(result & ~7);
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
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
