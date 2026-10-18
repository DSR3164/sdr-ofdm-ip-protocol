#pragma once

#include <complex>
#include <cstddef>
#include <vector>

using Complex = std::complex<float>;

constexpr size_t RB_SIZE = 12;

class ResourceGrid {
  public:
    ResourceGrid(size_t n_symbols, size_t n_rbs);

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
  private:
    size_t n_symbols_;
    size_t n_rbs_;

    std::vector<Complex> data_;
};
