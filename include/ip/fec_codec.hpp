#pragma once

#include <array>
#include <cstdint>
#include <vector>

struct TransitionTarget {
    uint8_t next_state;
    uint8_t b1;
    uint8_t b2;
};

enum class CodeRate
{
    R_1_2,
    R_3_4
};

// Runtime puncturing config, selected at startup from CLI.
struct PunctConfig {
    int period;
    int kept;
    std::array<bool, 6> mask; // max period size we support = 6, unused tail ignored
};

// Returns the puncturing config for the given rate.
PunctConfig make_punct_config(CodeRate rate);

std::vector<uint8_t> interleaving_(const std::vector<uint8_t> &input);
std::vector<float> deinterleaving_float(const std::vector<float> &input);
std::vector<uint8_t> conv_encoder(const std::vector<uint8_t> &bytes);
std::vector<uint8_t> viterbi_decoder_llr(const std::vector<float> &llr);

std::vector<uint8_t> puncture(const std::vector<uint8_t> &coded_bits, const PunctConfig &cfg);
std::vector<float> depuncture(const std::vector<float> &llr, const PunctConfig &cfg);

constexpr uint8_t g_1 = 7;
constexpr uint8_t g_2 = 5;
constexpr int m = 2;
constexpr int n = 2;
constexpr uint8_t num_states = 1 << m;
constexpr int VINF = 100000;
