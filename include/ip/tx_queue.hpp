#pragma once

#include "ip/scheduled_frame.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

class TxQueue {
  public:
    explicit TxQueue(size_t max_batch = 16, std::chrono::microseconds flush_timeout = std::chrono::microseconds(500), size_t max_pending = 512)
        : m_max_batch(max_batch),
          m_timeout(flush_timeout),
          m_max_pending(max_pending)
    {
    }

    bool push_many(std::vector<ScheduledFrame> frames, bool priority = false)
    {
        if (frames.empty())
            return true;

        const auto now = std::chrono::steady_clock::now();
        {
            std::scoped_lock lock(m_mtx);
            if (m_stopped)
                return false;

            if (!priority && m_q.size() + frames.size() > m_max_pending)
            {
                m_dropped.fetch_add(frames.size(), std::memory_order_relaxed);
                return false;
            }
            if (m_q.empty())
            {
                m_flush_deadline = now + m_timeout;
                m_timer_active = true;
            }
            if (priority)
            {
                for (auto it = frames.rbegin(); it != frames.rend(); ++it)
                {
                    it->enqueued = now;
                    m_q.push_front(std::move(*it));
                }
            }
            else
            {
                for (auto &f : frames)
                {
                    f.enqueued = now;
                    m_q.push_back(std::move(f));
                }
            }
        }

        return true;
    }

    bool push(ScheduledFrame frame, bool priority = false)
    {
        std::vector<ScheduledFrame> v;
        v.push_back(std::move(frame));
        return push_many(std::move(v), priority);
    }

    size_t pop_batch(std::vector<ScheduledFrame> &out)
    {
        out.clear();
        std::scoped_lock lock(m_mtx);

        if (m_q.empty())
            return 0;
        const auto now = std::chrono::steady_clock::now();

        const bool batch_full = m_q.size() >= m_max_batch;
        const bool timeout = m_timer_active && now >= m_flush_deadline;

        if (!batch_full && !timeout)
            return 0;

        const size_t n = std::min(m_q.size(), m_max_batch);
        out.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            out.push_back(std::move(m_q.front()));
            m_q.pop_front();
        }
        if (m_q.empty())
            m_timer_active = false;

        return out.size();
    }

    void stop()
    {
        {
            std::scoped_lock lock(m_mtx);
            m_stopped = true;
        }
    }

    size_t size() const
    {
        std::scoped_lock lock(m_mtx);
        return m_q.size();
    }
    size_t dropped() const { return m_dropped.load(std::memory_order_relaxed); }
  private:
    mutable std::mutex m_mtx;
    std::deque<ScheduledFrame> m_q;
    bool m_stopped = false;

    const size_t m_max_batch;
    const std::chrono::microseconds m_timeout;
    const size_t m_max_pending;
    std::atomic<size_t> m_dropped{ 0 };

    std::chrono::steady_clock::time_point m_flush_deadline{};
    bool m_timer_active = false;
};
