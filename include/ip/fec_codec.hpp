#pragma once

#include <cstdint>
#include <vector>

std::vector<uint32_t> hamming_encoder(const std::vector<uint8_t> &bytes);
std::vector<uint8_t> hamming_decoder(const std::vector<uint32_t> &encoded_bytes);
std::vector<uint32_t> interleaving(const std::vector<uint32_t> &input);
std::vector<uint32_t> deinterleaving(const std::vector<uint32_t> &input);