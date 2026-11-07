#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
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

    void from_bins(size_t symbol, const std::vector<Complex> &bins);

    void to_bins(size_t symbol, std::vector<Complex> &bins) const;

    void fill_symbol(
        size_t symbol,
        std::span<const Complex> data,
        const std::vector<size_t> &pilot_bins,
        Complex pilot
    );

    size_t active_subcarriers() const;
    bool allocate_rb(size_t rb, uint16_t rnti);
    void release_rb(size_t rb);
    uint16_t owner_of(size_t rb) const;
  private:
    size_t n_symbols_;
    size_t n_rbs_;
    size_t n_fft_;
    std::vector<uint16_t> owners_;
    std::vector<Complex> data_;
};
