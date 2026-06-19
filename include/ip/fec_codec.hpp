#pragma once

#include <array>
#include <cstdint>
#include <vector>

struct TransitionTarget {
    uint8_t next_state;
    uint8_t b1;
    uint8_t b2;
};

// Puncturing pattern for rate 3/4 (from rate 1/2 mother code)
// period = 3 input symbols -> 6 coded bits -> keep 4
constexpr int punct_period = 6;
constexpr int punct_kept = 4;
constexpr std::array<bool, punct_period> punct_mask = {
    true, true, false, true, true, false
};
// index:  X1    Y1    X2    Y2    X3    Y3
// kept:   X1    Y1          Y2    X3

std::vector<uint8_t> interleaving_(const std::vector<uint8_t> &input);
std::vector<float> deinterleaving_float(const std::vector<float> &input);

std::vector<uint8_t> conv_encoder(const std::vector<uint8_t> &bytes);
std::vector<uint8_t> viterbi_decoder_llr(const std::vector<float> &llr);

std::vector<uint8_t> puncture(const std::vector<uint8_t> &coded_bits);
std::vector<float> depuncture(const std::vector<float> &llr);

constexpr uint8_t g_1 = 7;
constexpr uint8_t g_2 = 5;
constexpr int m = 2;
constexpr int n = 2;
constexpr uint8_t num_states = 1 << m;
constexpr int VINF = 100000;
