#pragma once

#include "common.hpp"
#include "phy/resource_grid.hpp"
#include "ip/scheduled_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class Scheduler {
  public:
    explicit Scheduler(size_t n_rbs);

    bool add_user(uint16_t rnti);
    bool remove_user(uint16_t rnti);

    void clear_users();

    bool get_pending_frames(SharedData &data);

    size_t round_robin_scheduling(ResourceGrid &grid, SharedData &data);

    size_t estimate_payload(std::vector<uint8_t> &payload);

    size_t user_count() const;
  private:
    size_t n_rbs_;
    size_t num_pending_frames_;

    bool all_pending_user_scheduled_ = true;

    std::vector<uint16_t> users_;
    std::vector<ScheduledFrame> pending_frames_;
    std::vector<ScheduledFrame> deferred_frames_;

    size_t next_user_ = 0;
};