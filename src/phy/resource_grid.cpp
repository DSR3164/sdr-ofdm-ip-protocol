#include "phy/resource_grid.hpp"

#include <algorithm>
#include <stdexcept>

ResourceGrid::ResourceGrid(size_t n_symbols, size_t n_rbs, size_t n_fft)
    : n_symbols_(n_symbols),
      n_rbs_(n_rbs),
      n_fft_(n_fft),
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

size_t ResourceGrid::column_count() const
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

    const size_t columns = column_count();

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
