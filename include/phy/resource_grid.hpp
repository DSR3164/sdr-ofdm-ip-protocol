#pragma once

#include <complex>
#include <cstddef>
#include <vector>

using Complex = std::complex<float>;

constexpr size_t RB_SIZE = 12;

class ResourceGrid {
  public:
    ResourceGrid(size_t n_symbols, size_t n_rbs, size_t n_fft);

    void clear();

    void set_re(
        size_t symbol,
        size_t rb,
        size_t subcarrier,
        Complex value
    );

    Complex get_re(
        size_t symbol,
        size_t rb,
        size_t subcarrier
    ) const;

    size_t bin_of(size_t column) const;

    void to_bins(size_t symbol, std::vector<Complex> &bins) const;
  private:
    size_t n_symbols_;
    size_t n_rbs_;
    size_t n_fft_;

    std::vector<Complex> data_;
};
