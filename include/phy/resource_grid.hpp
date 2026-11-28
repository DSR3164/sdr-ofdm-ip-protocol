#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

using Complex = std::complex<float>;

constexpr size_t RB_SIZE = 12;
constexpr size_t num_ofdm_symbols = 10;

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
    size_t get_remaining_rb_num() const;
    std::map<uint16_t, std::vector<std::pair<uint8_t, uint8_t>>> get_rntis_rbs(std::vector<uint16_t> rntis);
  private:
    size_t n_symbols_;
    size_t n_rbs_;
    size_t n_fft_;
    std::vector<uint16_t> owners_;
    std::vector<Complex> data_;
    std::map<uint16_t, std::vector<std::pair<uint8_t, uint8_t>>> rntis_to_rb_;
};
