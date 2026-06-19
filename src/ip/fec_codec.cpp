#include "logger.hpp"
#include "ip/fec_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

std::vector<uint8_t> conv_encoder(const std::vector<uint8_t> &bytes)
{
    size_t out_bits = (bytes.size() * 8 + m) * n;
    std::vector<uint8_t> encoded_bytes((out_bits + 7) / 8, 0);

    uint8_t state = 0;
    size_t byte_in_pos = 0;
    size_t byte_out_pos = 0;
    int bit_in_pos = 0;
    int bit_out_pos = 0;

    auto encode_bit = [&](uint8_t u)
    {
        uint8_t win = (state << 1) | u;
        uint8_t b1 = (__builtin_popcount(win & g_1)) % 2;
        uint8_t b2 = (__builtin_popcount(win & g_2)) % 2;
        encoded_bytes[byte_out_pos] |= (b1 << (7 - bit_out_pos));
        if (++bit_out_pos == 8)
        {
            bit_out_pos = 0;
            byte_out_pos++;
        }
        encoded_bytes[byte_out_pos] |= (b2 << (7 - bit_out_pos));
        if (++bit_out_pos == 8)
        {
            bit_out_pos = 0;
            byte_out_pos++;
        }
        state = win & ((1 << m) - 1);
    };

    while (byte_in_pos < bytes.size())
    {
        uint8_t u = (bytes[byte_in_pos] >> (7 - bit_in_pos)) & 1U;
        if (++bit_in_pos == 8)
        {
            bit_in_pos = 0;
            byte_in_pos++;
        }
        encode_bit(u);
    }

    for (int f = 0; f < m; ++f)
        encode_bit(0);

    return encoded_bytes;
}

std::vector<uint8_t> viterbi_decoder_llr(const std::vector<float> &llr)
{
    if (llr.empty())
        return {};

    size_t T = llr.size() / 2;

    logs::tun.trace("[VIT] llr.size() = {}", llr.size());
    logs::tun.trace("[VIT] T = {}", T);

    std::array<std::array<TransitionTarget, 2>, num_states> transitions;
    for (uint8_t state = 0; state < num_states; ++state)
    {
        for (uint8_t input_bit = 0; input_bit < 2; ++input_bit)
        {
            uint8_t win = (state << 1) | input_bit;
            uint8_t b1 = (__builtin_popcount(win & g_1)) % 2;
            uint8_t b2 = (__builtin_popcount(win & g_2)) % 2;
            uint8_t next_state = win & ((1 << m) - 1);
            transitions[state][input_bit] = { next_state, b1, b2 };
        }
    }
    std::array<int, num_states> cur_metrics;
    cur_metrics[0] = 0;
    std::fill(cur_metrics.begin() + 1, cur_metrics.end(), VINF);
    std::vector<int> history(T * num_states, -1);

    for (size_t t = 0; t < T; ++t)
    {
        if (t * 2 + 1 >= llr.size())
        {
            logs::tun.error("[VIT] OOB llr access: t={}, idx2={}, size={}", t, t * 2 + 1, llr.size());
            return {};
        }
        std::array<int, num_states> next_metrics;
        std::fill(next_metrics.begin(), next_metrics.end(), VINF);
        for (uint8_t prev_state = 0; prev_state < num_states; ++prev_state)
        {
            if (cur_metrics[prev_state] == VINF)
            {
                continue;
            }
            for (uint8_t input_bit = 0; input_bit < 2; ++input_bit)
            {
                uint8_t next_state = transitions[prev_state][input_bit].next_state;
                uint8_t u1 = transitions[prev_state][input_bit].b1;
                uint8_t u2 = transitions[prev_state][input_bit].b2;
                float ham_s = (u1 == 0 ? -llr[t * 2] : llr[t * 2]) + (u2 == 0 ? -llr[t * 2 + 1] : llr[t * 2 + 1]);
                int candidate = cur_metrics[prev_state] + ham_s;
                if (candidate < next_metrics[next_state])
                {
                    next_metrics[next_state] = candidate;
                    size_t idx = t * num_states + next_state;

                    if (idx >= history.size())
                    {
                        logs::tun.error("[VIT] history OOB idx={} size={}", idx, history.size());
                        return {};
                    }

                    history[idx] = prev_state;
                }
            }
        }
        cur_metrics = next_metrics;
    }

    uint8_t cur_state = 0;
    std::vector<uint8_t> decoded_bytes((T + 7) / 8, 0);

    for (int t = (int)T - 1; t >= 0; --t)
    {
        size_t idx = t * num_states + cur_state;
        if (idx >= history.size())
        {
            logs::tun.error("[VIT] TRACEBACK OOB idx={} cur_state={} t={}", idx, cur_state, t);
            return {};
        }

        int16_t prev_state = history[idx];
        if (prev_state < 0 || prev_state >= num_states)
        {
            logs::tun.error("[VIT] INVALID STATE prev_state={} t={}", prev_state, t);
            return {};
        }

        int bit_index = t;
        int byte_pos = bit_index / 8;
        int bit_pos = bit_index % 8;

        uint8_t input_bit = cur_state & 1U;
        decoded_bytes[byte_pos] |= (input_bit << (7 - bit_pos));

        cur_state = prev_state;
    }
    return decoded_bytes;
}

std::vector<uint8_t> interleaving_(const std::vector<uint8_t> &input)
{
    size_t c = 8;
    size_t n = input.size();
    size_t r = n / c;

    std::vector<uint8_t> deint_llr;
    deint_llr.reserve(n);

    for (size_t i = 0; i < c; ++i)
        for (size_t j = 0; j < r; ++j)
            deint_llr.push_back(input[j * c + i]);

    return deint_llr;
}

std::vector<float> deinterleaving_float(const std::vector<float> &input)
{
    size_t c = 8;
    size_t n = input.size();
    size_t r = n / c;

    std::vector<float> deint(n);
    size_t idx = 0;

    for (size_t i = 0; i < c; ++i)
        for (size_t j = 0; j < r; ++j)
            deint[j * c + i] = input[idx++];

    return deint;
}

PunctConfig make_punct_config(CodeRate rate)
{
    switch (rate)
    {
    case CodeRate::R_1_2:
        return PunctConfig{ 2, 2, { true, true, false, false, false, false } };
    case CodeRate::R_3_4:
        return PunctConfig{ 6, 4, { true, true, false, true, true, false } };
    }
    return PunctConfig{ 2, 2, { true, true, false, false, false, false } };
}

std::vector<uint8_t> puncture(const std::vector<uint8_t> &coded_bits, const PunctConfig &cfg)
{
    std::vector<uint8_t> out;
    out.reserve(coded_bits.size());

    for (size_t i = 0; i < coded_bits.size(); ++i)
        if (cfg.mask[i % cfg.period])
            out.push_back(coded_bits[i]);

    return out;
}

std::vector<float> depuncture(const std::vector<float> &llr, const PunctConfig &cfg)
{
    size_t periods = (llr.size() + cfg.kept - 1) / cfg.kept;
    std::vector<float> out(periods * cfg.period, 0.0f);

    size_t in_idx = 0;
    for (size_t i = 0; i < out.size() && in_idx < llr.size(); ++i)
        if (cfg.mask[i % cfg.period])
            out[i] = llr[in_idx++];

    return out;
}
