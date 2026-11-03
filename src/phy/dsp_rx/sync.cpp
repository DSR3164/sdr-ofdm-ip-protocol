#include "phy/dsp_rx/sync.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

size_t correlation_fft_length(size_t signal_length, size_t reference_length)
{
    const size_t required = signal_length + reference_length - 1;
    size_t result = 1;
    while (result < required)
    {
        if (result > static_cast<size_t>(std::numeric_limits<int>::max()) / 2)
            throw std::invalid_argument("Correlation FFT is too large");
        result *= 2;
    }
    return result;
}

ZCCorrFFT::ZCCorrFFT(size_t signal_length, const std::vector<std::complex<float>> &reference)
    : reference_length_(reference.size()),
      max_signal_length_(signal_length),
      forward_(correlation_fft_length(signal_length, reference.size())),
      backward_(forward_.size, false),
      reference_spectrum_(forward_.size)
{
    forward_.clear();
    for (size_t i = 0; i < reference.size(); ++i)
    {
        forward_.in[i][0] = reference[i].real();
        forward_.in[i][1] = reference[i].imag();
        reference_energy_ += std::norm(std::complex<float>(reference[i]));
    }
    if (reference_energy_ <= 0.0)
        throw std::invalid_argument("Zero-energy synchronization reference");
    fftwf_execute(forward_.plan);
    for (size_t i = 0; i < forward_.size; ++i)
        reference_spectrum_[i] = { forward_.out[i][0], -forward_.out[i][1] };
}

int ZCCorrFFT::find_peak(std::span<const std::complex<float>> signal, float threshold)
{
    if (signal.size() <= reference_length_)
        return -1;
    if (signal.size() > max_signal_length_)
        throw std::invalid_argument("Correlation input exceeds the planned size");

    forward_.clear();
    for (size_t i = 0; i < signal.size(); ++i)
    {
        forward_.in[i][0] = static_cast<float>(signal[i].real());
        forward_.in[i][1] = static_cast<float>(signal[i].imag());
    }
    fftwf_execute(forward_.plan);
    for (size_t i = 0; i < forward_.size; ++i)
    {
        const std::complex<float> value = std::complex<float>(forward_.out[i][0], forward_.out[i][1]) * reference_spectrum_[i];
        backward_.in[i][0] = value.real();
        backward_.in[i][1] = value.imag();
    }
    fftwf_execute(backward_.plan);

    const size_t count = signal.size() - reference_length_ + 1;
    metrics_.resize(count);
    double energy = 0.0;
    for (size_t i = 0; i < reference_length_; ++i)
        energy += std::norm(std::complex<float>(signal[i]));
    const double normalization = 1.0 / static_cast<double>(forward_.size);
    for (size_t i = 0; i < count; ++i)
    {
        if (i != 0)
        {
            energy += std::norm(std::complex<float>(signal[i + reference_length_ - 1])) - std::norm(std::complex<float>(signal[i - 1]));
            energy = std::max(energy, 0.0);
        }
        const std::complex<float> corr(backward_.out[i][0] * normalization, backward_.out[i][1] * normalization);
        metrics_[i] = energy > 1e-12 ? std::norm(corr) / (energy * reference_energy_) : 0.0;
    }
    for (size_t i = 0; i + 1 < count; ++i)
        if (metrics_[i] >= threshold && (i == 0 || metrics_[i] >= metrics_[i - 1]) && metrics_[i] >= metrics_[i + 1])
            return static_cast<int>(i);
    return -1;
}
