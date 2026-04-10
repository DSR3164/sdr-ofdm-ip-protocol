#pragma once

#include <cstdint>
#include <vector>

template <typename T>
std::vector<uint8_t> byte_to_bits(const std::vector<T> &data, int16_t r)
{
    std::vector<uint8_t> bits;
    bits.reserve(data.size() * r);

    for (size_t i = 0; i < data.size(); ++i)
    {
        for (int j = r - 1; j >= 0; --j)
        {
            bits.push_back(static_cast<uint8_t>((data[i] >> j) & 1));
        }
    }
    return bits;
}

template <typename T>
std::vector<T> bits_to_bytes(const std::vector<uint8_t> &bits, int16_t r)
{
    size_t full_elements = bits.size() / r;
    std::vector<T> result(full_elements, 0);

    for (size_t i = 0; i < full_elements * r; ++i)
    {
        if (bits[i])
        {
            result[i / r] |= (static_cast<T>(1) << ((r - 1) - (i % r)));
        }
    }
    return result;
}
