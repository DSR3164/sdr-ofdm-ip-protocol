#include "phy/scheduler.hpp"

#include <algorithm>
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

size_t Scheduler::schedule(ResourceGrid &grid)
{
    for (size_t rb = 0; rb < n_rbs_; ++rb)
    {
        grid.release_rb(rb);
    }

    if (users_.empty())
    {
        return 0;
    }

    for (size_t rb = 0; rb < n_rbs_; ++rb)
    {
        grid.allocate_rb(rb, users_[next_user_]);

        next_user_ = (next_user_ + 1) % users_.size();
    }

    return n_rbs_;
}

size_t Scheduler::user_count() const
{
    return users_.size();
}