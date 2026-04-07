#include "ip/ip_layer.hpp"

std::vector<uint8_t> byte_to_bits(const uint8_t *bytes, size_t size)
{
    std::vector<uint8_t> bits;
    bits.reserve(size * 8);
    for (size_t i = 0; i < size; ++i)
    {
        for (int j = 7; j >= 0; --j)
        {
            bits.push_back((bytes[i] >> j) & 1);
        }
    }
    return bits;
}


std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits)
{
    size_t full_bytes = bits.size() / 8;
    std::vector<uint8_t> bytes(full_bytes, 0);

    for (size_t i = 0; i < full_bytes * 8; ++i) {
        if (bits[i]) {
            bytes[i / 8] |= (1 << (7 - (i % 8)));
        }
    }
    return bytes;
}