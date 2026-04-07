#include "ip/ip_layer.hpp"

std::vector<uint8_t> byte_to_bits(uint8_t *bytes, IP &ip)
{
    std::vector<uint8_t> bits;
    ssize_t n = ip.nbytes.load();
    bits.reserve(n * 8);
    for (size_t i = 0; (ssize_t)i < n; ++i)
    {
        for (int j = 7; j >= 0; --j)
        {
            uint8_t bit = (bytes[i] >> j) & 1;
            bits.push_back(bit);
        }
    }

    return bits;
}
