#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

// Thread-safe FIFO used to hand work between threads (UI <-> network,
// server reader <-> writer). close() wakes every waiter; afterwards push()
// is refused while pop() keeps returning queued items until drained.
template <typename T>
class BlockingQueue {
public:
    bool push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_)
                return false;
            items_.push_back(std::move(value));
        }
        ready_.notify_one();
        return true;
    }

    // Blocks until an item is available, or returns nullopt once the queue
    // is closed and empty.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return closed_ || !items_.empty(); });
        return takeFrontLocked();
    }

    template <typename Rep, typename Period>
    std::optional<T> popFor(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait_for(lock, timeout, [this] { return closed_ || !items_.empty(); });
        return takeFrontLocked();
    }

    // Non-blocking: takes everything currently queued.
    std::vector<T> drain() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<T> out;
        out.reserve(items_.size());
        for (T& item : items_)
            out.push_back(std::move(item));
        items_.clear();
        return out;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        ready_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

private:
    std::optional<T> takeFrontLocked() {
        if (items_.empty())
            return std::nullopt;
        T value = std::move(items_.front());
        items_.pop_front();
        return value;
    }

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<T> items_;
    bool closed_ = false;
};
