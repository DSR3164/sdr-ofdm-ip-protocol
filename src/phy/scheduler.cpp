#include "common.hpp"
#include "phy/scheduler.hpp"
#include "ip/scheduled_frame.hpp"
#include "ip/tx_queue.hpp"

#include <SDL2/SDL_gamecontroller.h>
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

    if (std::find(users_.begin(), users_.end(), rnti) != users_.end())
    {
        return false;
    }

    users_.push_back(rnti);

    return true;
}

bool Scheduler::remove_user(uint16_t rnti)
{
    auto it = std::find(users_.begin(), users_.end(), rnti);

    if (it == users_.end())
    {
        return false;
    }

    size_t index = std::distance(users_.begin(), it);

    users_.erase(it);

    if (users_.empty())
    {
        next_user_ = 0;
    }
    else
    {
        if (index < next_user_)
        {
            --next_user_;
        }

        next_user_ %= users_.size();
    }

    return true;
}

void Scheduler::clear_users()
{
    users_.clear();
    next_user_ = 0;
}

bool Scheduler::get_pending_frames(SharedData &data)
{
    if (!deferred_frames_.empty())
        return false;

    num_pending_frames_ = data.tx_queue.pop_batch(pending_frames_);
    if (num_pending_frames_ == 0)
    {
        return false;
    }

    for (const auto &user : pending_frames_)
    {
        if (!add_user(user.rnti))
            continue;
    }

    // TODO:
    // В будущем тут стоит добавить вызов `get_scheduler_users()` для получения RNTI <-> IP

    return true;
}

size_t Scheduler::round_robin_scheduling(ResourceGrid &grid, SharedData &data)
{
    for (size_t rb = 0; rb < n_rbs_; ++rb)
    {
        grid.release_rb(rb);
    }

    bool is_pending_frames = false;
    if (deferred_frames_.empty() && pending_frames_.empty())
        is_pending_frames = get_pending_frames(data);

    if (users_.empty())
    {
        return 0;
    }

    size_t last_rb = 0;
    if (is_pending_frames)
    {
        for (size_t i = 0; i < pending_frames_.size(); ++i)
        {
            size_t needed_rb = estimate_payload(pending_frames_[i].payload);
            if (last_rb + needed_rb > n_rbs_)
            {
                deferred_frames_.push_back(pending_frames_[i]);
                continue;
            }
            for (size_t rb = last_rb; rb < (last_rb + needed_rb); ++rb)
                grid.allocate_rb(rb, pending_frames_[i].rnti);

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
                if (last_rb + needed_rb > n_rbs_)
                {
                    remaining_frames.push_back(deferred_frames_[i]);
                    continue;
                }
                for (size_t rb = last_rb; rb < (last_rb + needed_rb); ++rb)
                    grid.allocate_rb(rb, deferred_frames_[i].rnti);
                last_rb += needed_rb;
            }
            deferred_frames_ = remaining_frames;
        }
    }

    // Случай, когда внутренняя очерель deferred_frames_ была распланирована, и надо вызывать новую через get_pending_frames()
    if (grid.get_remaining_rb_num() > 0 && deferred_frames_.empty())
    {
        if (get_pending_frames(data))
        {
            for (size_t i = 0; i < pending_frames_.size(); ++i)
            {
                size_t needed_rb = estimate_payload(pending_frames_[i].payload);
                if (last_rb + needed_rb > n_rbs_)
                {
                    deferred_frames_.push_back(pending_frames_[i]);
                    continue;
                }
                for (size_t rb = last_rb; rb < (last_rb + needed_rb); ++rb)
                    grid.allocate_rb(rb, pending_frames_[i].rnti);

                last_rb += needed_rb;
            }
        }
        pending_frames_.clear();
    }

    return last_rb;
}

size_t Scheduler::estimate_payload(std::vector<uint8_t> &payload)
{
    size_t needed_bits = payload.size();
    size_t rb_size = 12 * 10 * 4;
    return (needed_bits + rb_size - 1) / rb_size;
}

size_t Scheduler::user_count() const
{
    return users_.size();
}