#include "phy/resource_grid.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

ResourceGrid::ResourceGrid(size_t n_symbols, size_t n_rbs, size_t n_fft)
    : n_symbols_(n_symbols),
      n_rbs_(n_rbs),
      n_fft_(n_fft),
      owners_(n_rbs, 0),
      data_(n_symbols * n_rbs * RB_SIZE, Complex{})
{
    if (n_symbols == 0 || n_rbs == 0)
    {
        throw std::invalid_argument(
            "ResourceGrid dimensions must be positive"
        );
    }
}

void ResourceGrid::clear()
{
    std::fill(data_.begin(), data_.end(), Complex{});
}

void ResourceGrid::set_re(
    size_t symbol,
    size_t rb,
    size_t subcarrier,
    Complex value
)
{
    if (symbol >= n_symbols_ || rb >= n_rbs_ || subcarrier >= RB_SIZE)
    {
        throw std::out_of_range("ResourceGrid index out of range");
    }

    data_[(symbol * n_rbs_ + rb) * RB_SIZE + subcarrier] = value;
}

Complex ResourceGrid::get_re(
    size_t symbol,
    size_t rb,
    size_t subcarrier
) const
{
    if (symbol >= n_symbols_ || rb >= n_rbs_ || subcarrier >= RB_SIZE)
    {
        throw std::out_of_range("ResourceGrid index out of range");
    }
    return data_[(symbol * n_rbs_ + rb) * RB_SIZE + subcarrier];
}

size_t ResourceGrid::bin_of(size_t column) const
{
    const size_t half = n_rbs_ * RB_SIZE / 2;
    if (column >= 2 * half)
        throw std::out_of_range("ResourceGrid column out of range");

    return column < half ? column + 1 : n_fft_ - half + (column - half);
}

void ResourceGrid::from_bins(
    size_t symbol,
    const std::vector<Complex> &bins
)
{
    if (symbol >= n_symbols_)
        throw std::out_of_range("ResourceGrid symbol out of range");

    if (bins.size() != n_fft_)
        throw std::invalid_argument("FFT bins have the wrong size");

    for (size_t column = 0; column < active_subcarriers(); ++column)
    {
        const size_t fft_bin = bin_of(column);

        data_[symbol * active_subcarriers() + column] = bins[fft_bin];
    }
}

void ResourceGrid::to_bins(size_t symbol, std::vector<Complex> &bins) const
{
    if (symbol >= n_symbols_)
        throw std::out_of_range("ResourceGrid symbol out of range");

    bins.assign(n_fft_, Complex{});

    for (size_t column = 0; column < n_rbs_ * RB_SIZE; ++column)
    {
        bins[bin_of(column)] = data_[symbol * n_rbs_ * RB_SIZE + column];
    }
}

size_t ResourceGrid::active_subcarriers() const
{
    return n_rbs_ * RB_SIZE;
}

void ResourceGrid::fill_symbol(
    size_t symbol,
    std::span<const Complex> data,
    const std::vector<size_t> &pilot_bins,
    Complex pilot
)
{
    if (symbol >= n_symbols_)
        throw std::out_of_range("ResourceGrid symbol out of range");

    const size_t columns = active_subcarriers();

    if (pilot_bins.size() > columns || data.size() > columns - pilot_bins.size())
    {
        throw std::invalid_argument(
            "Too many data or pilot carriers"
        );
    }

    size_t data_index = 0;

    for (size_t column = 0; column < columns; ++column)
    {
        const size_t fft_bin = bin_of(column);

        const bool is_pilot = std::binary_search(
            pilot_bins.begin(),
            pilot_bins.end(),
            fft_bin
        );

        Complex value{};

        if (is_pilot)
        {
            value = pilot;
        }
        else if (data_index < data.size())
        {
            value = data[data_index];
            ++data_index;
        }
        else
        {
            ++data_index;
        }

        set_re(
            symbol,
            column / RB_SIZE,
            column % RB_SIZE,
            value
        );
    }
}

bool ResourceGrid::allocate_rb(size_t rb, uint16_t rnti)
{
    if (rb >= n_rbs_)
        throw std::out_of_range("ResourceGrid RB index out of range");

    if (rnti == 0)
        throw std::invalid_argument("RNTI 0 is reserved");

    if (owners_[rb] != 0)
        return false;

    owners_[rb] = rnti;
    return true;
}

void ResourceGrid::release_rb(size_t rb)
{
    if (rb >= n_rbs_)
        throw std::out_of_range("ResourceGrid RB index out of range");

    owners_[rb] = 0;
}

uint16_t ResourceGrid::owner_of(size_t rb) const
{
    if (rb >= n_rbs_)
        throw std::out_of_range("ResourceGrid RB index out of range");

    return owners_[rb];
}

std::map<uint16_t, std::vector<std::pair<uint8_t, uint8_t>>> ResourceGrid::get_rntis_rbs(std::vector<uint16_t> rntis)
{
    rntis_to_rb_.clear();
    for (const auto &rnti : rntis)
    {
        size_t cursor = 0;
        while (cursor < n_rbs_ && owner_of(cursor) != rnti)
            ++cursor;

        if (cursor >= n_rbs_)
            continue;

        size_t rb_start = cursor;
        size_t rb_num = 0;

        while (cursor < n_rbs_ && owner_of(cursor) == rnti)
        {
            ++rb_num;
            ++cursor;
        }

        rntis_to_rb_[rnti].emplace_back(rb_start, rb_num);
    }
    return rntis_to_rb_;
}

size_t ResourceGrid::get_remaining_rb_num() const
{
    size_t remaining_rb_num = 0;
    for (const auto &owner : owners_)
    {
        if (owner == 0)
            remaining_rb_num++;
    }
    return remaining_rb_num;
}