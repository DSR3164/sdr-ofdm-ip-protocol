#include "common.hpp"
#include "phy/scheduler.hpp"
#include "ip/tx_queue.hpp"

#include <SDL2/SDL_gamecontroller.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

Scheduler::Scheduler(size_t n_rbs)
    : n_rbs_(n_rbs)
{
    if (n_rbs == 0)
    {
        throw std::invalid_argument(
            "Scheduler must have at least one RB"
        );
    }
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

    if (users_.empty())
    {
        return 0;
    }

    if (get_pending_frames(data))
    {
        // TODO:
        // Выделять RB в зависимости от payload для frame
        for (size_t rb = 0; rb < n_rbs_; ++rb)
        {
            grid.allocate_rb(rb, users_[next_user_]);

            next_user_ = (next_user_ + 1) % users_.size();
        }
    }

    return n_rbs_;
}

size_t Scheduler::user_count() const
{
    return users_.size();
}