#pragma once

#include <cstdint>
#include <vector>

struct TransitionTarget {
    uint8_t next_state;
    uint8_t b1;
    uint8_t b2;
};

std::vector<uint32_t> hamming_encoder(const std::vector<uint8_t> &bytes);
std::vector<uint8_t> hamming_decoder(const std::vector<uint32_t> &encoded_bytes);
std::vector<uint8_t> interleaving(const std::vector<uint8_t> &input);
std::vector<uint8_t> deinterleaving(const std::vector<uint8_t> &input);

std::vector<uint8_t> conv_encoder(const std::vector<uint8_t> &bytes);
std::vector<uint8_t> viterbi_decoder(const std::vector<uint8_t> &bytes);

constexpr uint8_t g_1 = 7;
constexpr uint8_t g_2 = 5;
constexpr int m = 2;
constexpr int n = 2;
constexpr uint8_t num_states = 1 << m;
constexpr int VINF = 100000;
