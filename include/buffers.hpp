#pragma once

#include "common.hpp"

struct Buffers {
    DoubleBuffer<std::complex<float>> sdr_raw;
    DoubleBuffer<std::complex<float>> dsp;
    DoubleBuffer<uint8_t> stats;
    DoubleBuffer<uint8_t> ip;
    DoubleBuffer<std::string> socket;

    Buffers(int size1 = 3840, int size2 = 3840)
        : sdr_raw(size1),
          dsp(size2)
    {
    }
};
