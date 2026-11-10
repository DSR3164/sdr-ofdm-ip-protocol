#pragma once

#include "phy/resource_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class Scheduler {
  public:
    explicit Scheduler(size_t n_rbs);

    bool add_user(uint16_t rnti);
    bool remove_user(uint16_t rnti);

    void clear_users();

    size_t schedule(ResourceGrid &grid);

    size_t user_count() const;
  private:
    size_t n_rbs_;

    std::vector<uint16_t> users_;

    size_t next_user_ = 0;
};