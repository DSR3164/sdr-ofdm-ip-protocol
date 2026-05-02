#include "ip/fec_codec.hpp"

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