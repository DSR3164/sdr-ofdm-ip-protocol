#include "ip/ip_layer.hpp"
#include <cassert>

std::vector<uint8_t> byte_to_bits(uint8_t *bytes, IP &ip)
{
    std::vector<uint8_t> bits;
    ssize_t n = ip.nbytes.load();
    bits.reserve(n * 8);
    for (ssize_t i = 0; i < n; ++i)
    {
        for (int j = 7; j >= 0; --j)
        {
            uint8_t bit = (bytes[i] >> j) & 1;
            bits.push_back(bit);
        }
    }

    return bits;
}


std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits)
{
    assert(bits.size() % 8 == 0);
    std::vector<uint8_t> bytes(bits.size() / 8, 0);

    for (size_t i = 0; i < bytes.size(); ++i)
        for (int j = 7; j >= 0; --j)
            bytes[i] |= bits[i * 8 + (7 - j)] << j;

    return bytes;
}
