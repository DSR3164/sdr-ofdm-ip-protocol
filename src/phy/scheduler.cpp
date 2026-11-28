#include "common.hpp"
#include "logger.hpp"
#include "phy/scheduler.hpp"
#include "ip/scheduled_frame.hpp"
#include "ip/tx_queue.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

Scheduler::Scheduler(size_t n_rbs)
    : n_rbs_(n_rbs)
{
    if (n_rbs == 0)
    {
        throw std::invalid_argument(
            "Scheduler must have at least one RB"
        );
    }

    deferred_frames_.reserve(16);
}

bool Scheduler::add_user(uint16_t rnti)
{
    if (rnti == 0)
    {
        throw std::invalid_argument("RNTI 0 is reserved");
    }

    if (std::find(rntis_.begin(), rntis_.end(), rnti) != rntis_.end())
    {
        return false;
    }

    rntis_.push_back(rnti);

    return true;
}

bool Scheduler::remove_user(uint16_t rnti)
{
    auto it = std::find(rntis_.begin(), rntis_.end(), rnti);

    if (it == rntis_.end())
    {
        return false;
    }

    size_t index = std::distance(rntis_.begin(), it);

    rntis_.erase(it);

    if (rntis_.empty())
    {
        next_user_ = 0;
    }
    else
    {
        if (index < next_user_)
        {
            --next_user_;
        }

        next_user_ %= rntis_.size();
    }

    return true;
}

void Scheduler::clear_users()
{
    rntis_.clear();
    next_user_ = 0;
}

bool Scheduler::get_pending_frames(SharedData &data)
{
    rntis_.clear();

    if (not deferred_frames_.empty())
        return false;

    num_pending_frames_ = data.tx_queue.pop_batch(pending_frames_);
    if (num_pending_frames_ == 0)
    {
        return false;
    }

    for (const auto &user : pending_frames_)
    {
        if (not add_user(user.rnti))
            continue;
    }

    // TODO:
    // В будущем тут стоит добавить вызов `get_scheduler_users()` для получения RNTI <-> IP

    return true;
}

size_t Scheduler::sequential_scheduling(ResourceGrid &grid, SharedData &data)
{
    scheduler_allocation_.clear();

    for (size_t rb = 0; rb < n_rbs_; ++rb)
        grid.release_rb(rb);

    bool is_pending_frames = false;
    if (deferred_frames_.empty() && pending_frames_.empty())
        is_pending_frames = get_pending_frames(data);

    if (rntis_.empty())
        return 0;

    size_t last_rb = 0;
    if (is_pending_frames)
    {
        for (size_t i = 0; i < pending_frames_.size(); ++i)
        {
            size_t needed_rb = estimate_payload(pending_frames_[i].payload);
            if (needed_rb == 0)
                continue;

            if (last_rb + needed_rb > n_rbs_)
            {
                deferred_frames_.push_back(pending_frames_[i]);
                continue;
            }

            size_t rb_start = last_rb;
            for (size_t rb = rb_start; rb < rb_start + needed_rb; ++rb)
                grid.allocate_rb(rb, pending_frames_[i].rnti);

            scheduler_allocation_.emplace_back(
                pending_frames_[i].rnti,
                rb_start,
                needed_rb,
                pending_frames_[i].payload
            );

            last_rb += needed_rb;
        }
        pending_frames_.clear();
    }
    else
    {
        if (!deferred_frames_.empty())
        {
            std::vector<ScheduledFrame> remaining_frames;
            remaining_frames.reserve(deferred_frames_.size());
            for (size_t i = 0; i < deferred_frames_.size(); ++i)
            {
                size_t needed_rb = estimate_payload(deferred_frames_[i].payload);
                if (needed_rb == 0)
                    continue;

                if (last_rb + needed_rb > n_rbs_)
                {
                    remaining_frames.push_back(deferred_frames_[i]);
                    continue;
                }
                size_t rb_start = last_rb;
                for (size_t rb = rb_start; rb < rb_start + needed_rb; ++rb)
                    grid.allocate_rb(rb, deferred_frames_[i].rnti);

                scheduler_allocation_.emplace_back(
                    deferred_frames_[i].rnti,
                    rb_start,
                    needed_rb,
                    deferred_frames_[i].payload
                );

                last_rb += needed_rb;
            }
            deferred_frames_ = remaining_frames;
        }
    }

    return last_rb;
}

std::vector<Allocation> Scheduler::scheduler_output(ResourceGrid &grid, SharedData &data)
{
    allocation_map_.clear();
    size_t rb_allocated = sequential_scheduling(grid, data);
    for (const auto &scheduler_allocation : scheduler_allocation_)
    {
        Allocation allocation{
            scheduler_allocation.rnti,
            scheduler_allocation.rb_start,
            scheduler_allocation.rb_num,
            0,
            10,
            Modulation::QAM64,
            DLChannel::DATA,
            scheduler_allocation.payload
        };
        allocation_map_.push_back(allocation);
    }
    return allocation_map_;
}

size_t Scheduler::estimate_payload(std::vector<uint8_t> &payload)
{
    size_t needed_bits = payload.size();
    size_t rb_size = 12 * 10 * 6; // QAM64(6), 12 subcarriers, 10 OFDM symbols
    size_t needed_rb_num = (needed_bits + rb_size - 1) / rb_size;
    if (needed_rb_num > n_rbs_)
    {
        logs::scheduler.warn("Frame payload need {} RB, when {} is maximum", needed_rb_num, n_rbs_);
        needed_rb_num = 0;
    }
    return needed_rb_num;
}

size_t Scheduler::user_count() const
{
    return rntis_.size();
}