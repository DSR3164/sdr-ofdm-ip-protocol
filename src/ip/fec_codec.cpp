#include "ip/fec_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

std::vector<uint8_t> conv_encoder(const std::vector<uint8_t> &bytes)
{
    std::vector<uint8_t> encoded_bytes((bytes.size() * n) + 1, 0);

    uint8_t state = 0;

    size_t byte_in_pos = 0;
    size_t byte_out_pos = 0;
    int bit_in_pos = 0;
    int bit_out_pos = 0;

    while (byte_in_pos < bytes.size())
    {
        uint8_t u = (bytes[byte_in_pos] >> (7 - bit_in_pos)) & 1U;
        if (++bit_in_pos == 8)
        {
            bit_in_pos = 0;
            byte_in_pos++;
        }

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
    }

    return encoded_bytes;
}

std::vector<uint8_t> viterbi_decoder(const std::vector<uint8_t> &bytes)
{
    size_t T = (bytes.size() * 8) / n;

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

    std::vector<std::vector<int>> history(T, std::vector<int>(num_states, -1));

    size_t byte_in_pos = 0;
    int bit_in_pos = 0;

    for (size_t t = 0; t < T; ++t)
    {
        uint8_t b1 = (bytes[byte_in_pos] >> (7 - bit_in_pos)) & 1U;
        if (++bit_in_pos == 8)
        {
            bit_in_pos = 0;
            byte_in_pos++;
        }

        uint8_t b2 = 0;
        if (byte_in_pos < bytes.size())
            b2 = (bytes[byte_in_pos] >> (7 - bit_in_pos)) & 1U;
        if (++bit_in_pos == 8)
        {
            bit_in_pos = 0;
            byte_in_pos++;
        }

        std::array<int, num_states> next_metrics;
        std::fill(next_metrics.begin(), next_metrics.end(), VINF);

        for (uint8_t prev_state = 0; prev_state < num_states; ++prev_state)
        {
            if (cur_metrics[prev_state] == VINF)
                continue;
            for (uint8_t input_bit = 0; input_bit < 2; ++input_bit)
            {
                uint8_t next_state = transitions[prev_state][input_bit].next_state;

                uint8_t u1 = transitions[prev_state][input_bit].b1;
                uint8_t u2 = transitions[prev_state][input_bit].b2;

                int ham_s = (b1 != u1) + (b2 != u2);

                int candidate = cur_metrics[prev_state] + ham_s;

                if (candidate < next_metrics[next_state])
                {
                    next_metrics[next_state] = candidate;
                    history[t][next_state] = prev_state;
                }
            }
        }

        cur_metrics = next_metrics;
    }

    int min_metric = VINF;
    uint8_t cur_state = 0;
    for (uint8_t s = 0; s < num_states; ++s)
    {
        if (cur_metrics[s] < min_metric)
        {
            min_metric = cur_metrics[s];
            cur_state = s;
        }
    }

    std::vector<uint8_t> decoded_bytes(T / 8, 0);

    int byte_pos = static_cast<int>(decoded_bytes.size()) - 1;
    int bit_pos = 0;

    for (int t = (int)T - 1; t >= 0; --t)
    {
        uint8_t prev_state = history[t][cur_state];

        uint8_t input_bit = cur_state & 1U;

        decoded_bytes[byte_pos] |= (input_bit << bit_pos);

        if (++bit_pos == 8)
        {
            bit_pos = 0;
            byte_pos--;
        }

        cur_state = prev_state;
    }

    return decoded_bytes;
}

std::vector<uint32_t> hamming_encoder(const std::vector<uint8_t> &bytes)
{
    if (bytes.size() < 1)
        return {};

    std::vector<uint8_t> padded = bytes;

    while (padded.size() % 3 != 0)
        padded.push_back(0);

    std::vector<uint32_t> encoded_bytes;
    encoded_bytes.reserve(padded.size() / 3);

    for (size_t i = 0; i + 2 < padded.size(); i += 3)
    {
        uint32_t data24 = (static_cast<uint32_t>(padded[i]) << 16) | (static_cast<uint32_t>(padded[i + 1]) << 8) | padded[i + 2];

        uint32_t block = 0;
        uint8_t checksum = 0;
        int data_bit_pos = 23;

        for (int j = 1; j <= 30; ++j)
        {
            if ((j & (j - 1)) == 0)
                continue;
            if (data_bit_pos >= 0)
            {
                if ((data24 >> data_bit_pos) & 1)
                {
                    block |= (1 << (j - 1));
                    checksum ^= j;
                }
                data_bit_pos--;
            }
        }

        for (int j = 0; j < 5; ++j)
            if ((checksum >> j) & 1)
                block |= (1U << ((1 << j) - 1));

        encoded_bytes.push_back(block);
    }

    return encoded_bytes;
}

std::vector<uint8_t> hamming_decoder(const std::vector<uint32_t> &encoded_bytes)
{
    if (encoded_bytes.size() < 1)
        return {};

    std::vector<uint32_t> blocks = encoded_bytes;

    std::vector<uint8_t> decoded_bytes;
    decoded_bytes.reserve(blocks.size() * 3);

    for (size_t i = 0; i < blocks.size(); ++i)
    {
        uint8_t syndrome = 0;
        uint32_t block = 0;

        int data_bit_pos = 23;

        for (int j = 1; j <= 30; ++j)
            if ((blocks[i] >> (j - 1)) & 1)
                syndrome ^= j;

        if (syndrome != 0)
            blocks[i] ^= (1U << (syndrome - 1));

        for (int j = 1; j <= 30; ++j)
        {
            if ((j & (j - 1)) == 0)
                continue;
            if (data_bit_pos < 0)
                break;
            if ((blocks[i] >> (j - 1)) & 1)
                block |= (1U << data_bit_pos);
            data_bit_pos--;
        }
        decoded_bytes.push_back((block >> 16) & 0xFF);
        decoded_bytes.push_back((block >> 8) & 0xFF);
        decoded_bytes.push_back(block & 0xFF);
    }

    return decoded_bytes;
}

std::vector<uint32_t> interleaving(const std::vector<uint32_t> &input)
{
    if (input.empty())
        return {};
    constexpr size_t B = 32;
    const size_t N = input.size();

    std::vector<uint32_t> output(N, 0);
    for (size_t g = 0; g < N * B; ++g)
    {
        size_t sw = g / B, sb = B - 1 - (g % B);
        uint32_t bit = (input[sw] >> sb) & 1U;
        size_t col = g / N, row = g % N;
        size_t dg = col * N + row;
        size_t dw = dg / B, db = B - 1 - (dg % B);
        output[dw] |= (bit << db);
    }
    return output;
}

std::vector<uint32_t> deinterleaving(const std::vector<uint32_t> &input)
{
    if (input.empty())
        return {};
    constexpr size_t B = 32;
    const size_t N = input.size();

    std::vector<uint32_t> output(N, 0);
    for (size_t g = 0; g < N * B; ++g)
    {
        size_t col = g / N, row = g % N;

        size_t sg = col * N + row;
        size_t sw = sg / B, sb = B - 1 - (sg % B);

        uint32_t bit = (input[sw] >> sb) & 1U;

        size_t dw = g / B, db = B - 1 - (g % B);
        output[dw] |= (bit << db);
    }
    return output;
}