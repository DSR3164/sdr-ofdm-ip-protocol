#include "phy/resource_grid.hpp"

#include <stdexcept>

ResourceGrid::ResourceGrid(size_t n_symbols, size_t n_rbs)
    : n_symbols_(n_symbols),
      n_rbs_(n_rbs),
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
