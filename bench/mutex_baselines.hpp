// SPDX-License-Identifier: MIT
// Lock-based baselines the lock-free containers are compared against.
#pragma once

#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

namespace lfc_bench {

/// Bounded std::queue guarded by a std::mutex, with the same try_* interface.
template <typename T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t capacity) : capacity_(capacity) {}

    [[nodiscard]] bool try_push(const T& value) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_) {
            return false;
        }
        queue_.push(value);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::queue<T> queue_;
};

/// Bounded std::vector stack guarded by a std::mutex.
template <typename T>
class MutexStack {
public:
    explicit MutexStack(std::size_t capacity) : capacity_(capacity) { items_.reserve(capacity); }

    [[nodiscard]] bool try_push(const T& value) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (items_.size() >= capacity_) {
            return false;
        }
        items_.push_back(value);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (items_.empty()) {
            return false;
        }
        out = std::move(items_.back());
        items_.pop_back();
        return true;
    }

private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::vector<T> items_;
};

/// Latest-value register guarded by a std::mutex (baseline for Seqlock and
/// TripleBuffer).
template <typename T>
class MutexRegister {
public:
    void store(const T& value) {
        const std::lock_guard<std::mutex> lock(mutex_);
        value_ = value;
    }

    [[nodiscard]] T load() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

private:
    mutable std::mutex mutex_;
    T value_{};
};

}  // namespace lfc_bench
