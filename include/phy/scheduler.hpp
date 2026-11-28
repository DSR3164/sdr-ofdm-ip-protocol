#pragma once

#include "common.hpp"
#include "phy/resource_grid.hpp"
#include "ip/scheduled_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

struct SchedulerAllocation {
    uint16_t rnti;
    uint8_t rb_start;
    uint8_t rb_num;
    std::vector<uint8_t> payload;
};

class Scheduler {
  public:
    explicit Scheduler(size_t n_rbs);

    bool add_user(uint16_t rnti);
    bool remove_user(uint16_t rnti);

    void clear_users();

    bool get_pending_frames(SharedData &data);

    size_t sequential_scheduling(ResourceGrid &grid, SharedData &data);

    std::vector<Allocation> scheduler_output(ResourceGrid &grid, SharedData &data);

    size_t estimate_payload(std::vector<uint8_t> &payload);

    size_t user_count() const;
  private:
    size_t n_rbs_;
    size_t num_pending_frames_;

    std::vector<uint16_t> rntis_;
    std::vector<SchedulerAllocation> scheduler_allocation_;
    std::vector<ScheduledFrame> pending_frames_;
    std::vector<ScheduledFrame> deferred_frames_;
    std::vector<Allocation> allocation_map_;

    size_t next_user_ = 0;
};