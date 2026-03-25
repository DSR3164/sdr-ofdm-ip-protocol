#pragma once

#include "common.hpp"

#include <SoapySDR/Device.hpp>
#include <fftw3.h>
#include <vector>

struct FFTWPlan
{
    std::vector<float> window;
    fftwf_complex *in = nullptr;
    fftwf_complex *out = nullptr;
    fftwf_plan plan = nullptr;

    FFTWPlan(int size, bool direction = true) : window(size)
    {
        for (int i = 0; i < size; ++i)
            window[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(size - 1));

        in = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));
        out = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));
        if (!in || !out)
            throw std::bad_alloc{};

        plan = fftwf_plan_dft_1d(size, in, out, direction ? FFTW_FORWARD : FFTW_BACKWARD, FFTW_MEASURE);
        if (!plan)
            throw std::runtime_error("fftwf_plan_dft_1d failed");
    }

    ~FFTWPlan()
    {
        if (plan)
            fftwf_destroy_plan(plan);
        if (in)
            fftwf_free(in);
        if (out)
            fftwf_free(out);
    }

    // move constructor
    FFTWPlan(FFTWPlan &&other) noexcept
        : window(std::move(other.window)),
        in(other.in),
        out(other.out),
        plan(other.plan)
    {
        other.in = nullptr;
        other.out = nullptr;
        other.plan = nullptr;
    }

    FFTWPlan &operator=(FFTWPlan &&other) noexcept
    {
        if (this != &other)
        {
            if (plan) fftwf_destroy_plan(plan);
            if (in)   fftwf_free(in);
            if (out)  fftwf_free(out);

            window = std::move(other.window);
            in = other.in;
            out = other.out;
            plan = other.plan;

            other.in = nullptr;
            other.out = nullptr;
            other.plan = nullptr;
        }
        return *this;
    }
    FFTWPlan(const FFTWPlan &) = delete;
    FFTWPlan &operator=(const FFTWPlan &) = delete;
};

struct DSP {
    float cfo = 0.0f;
    int max_index = 0;
    int mod = 3;
    float sample_rate = 1.92e6;
    struct OFDMConfig {
        int mod = 2;
        int n_subcarriers = 128;
        int pilot_spacing = 6;
        int n_cp = 32;
    } ofdm_cfg;
};

int run_dsp(SharedData &data);
