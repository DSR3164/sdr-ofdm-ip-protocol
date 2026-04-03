#include <vector>
#include <cstdint>

std::vector<uint8_t> calculateCRC16(const std::vector<uint8_t> &data)
{
    uint16_t crc = 0xFFFF;
    uint16_t polynomial = 0x1021;

    for (uint8_t byte : data) {
        crc ^= (uint16_t) byte << 8;

        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ polynomial;
            else
                crc <<= 1;
        }
    }

    std::vector<uint8_t> crc_bytes(2);
    crc_bytes[0] = (crc >> 8) & 0xFF;
    crc_bytes[1] = crc & 0xFF;
    return crc_bytes;
}