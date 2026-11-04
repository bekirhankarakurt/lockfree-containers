// SPDX-License-Identifier: MIT
/// @file mpmc_queue.hpp
/// @brief Bounded multi-producer / multi-consumer queue (Dmitry Vyukov's design).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "lfc/config.hpp"

namespace lfc {

/// Bounded FIFO safe for any number of producers and consumers.
///
/// Every cell carries a sequence number that encodes its state relative to
/// the global enqueue/dequeue positions:
///  - `seq == pos`           : cell is free, producer for `pos` may write it
///  - `seq == pos + 1`       : cell holds data, consumer for `pos` may read it
///  - `seq == pos + capacity`: consumer finished, free for the next lap
///
/// Producers and consumers only contend on their own position counter
/// (one CAS each); the data hand-off itself is a plain acquire/release on the
/// cell, so the queue is lock-free and scales far better than a mutex.
///
/// `try_push` reports "full" and `try_pop` reports "empty" based on the cell at
/// the current position; with in-flight operations on other cells these
/// results are conservative (see README, "Memory ordering").
template <typename T>
class MpmcQueue {
    static_assert(std::is_nothrow_destructible_v<T>, "T must be nothrow destructible");
    // A throwing move after a cell is claimed would leave that cell stuck in the
    // "being read" state forever, so moves must not throw.
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move constructible and assignable");

public:
    using value_type = T;

    /// @param min_capacity Minimum number of elements; rounded up to a power of
    ///        two, at least 2.
    /// @throws std::invalid_argument if @p min_capacity is zero.
    explicit MpmcQueue(std::size_t min_capacity)
        : capacity_(validated(min_capacity)),
          mask_(capacity_ - 1),
          cells_(std::make_unique<Cell[]>(capacity_)) {  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        for (std::size_t i = 0; i < capacity_; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;
    MpmcQueue(MpmcQueue&&) = delete;
    MpmcQueue& operator=(MpmcQueue&&) = delete;

    ~MpmcQueue() {
        // Destroy any elements still in the queue (no concurrent access allowed).
        std::size_t pos = dequeue_pos_.value.load(std::memory_order_relaxed);
        const std::size_t end = enqueue_pos_.value.load(std::memory_order_relaxed);
        for (; pos != end; ++pos) {
            cells_[pos & mask_].storage.ptr()->~T();
        }
    }

    /// Constructs an element in place. Lock-free, any thread.
    /// @return false if the queue was observed full.
    template <typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept(
        std::is_nothrow_constructible_v<T, Args&&...>) {
        Cell* cell = nullptr;
        std::size_t pos = enqueue_pos_.value.load(std::memory_order_relaxed);
        detail::Backoff backoff;
        for (;;) {
            cell = &cells_[pos & mask_];
            // acquire: pairs with the consumer's release store of `sequence`, so
            // the previous occupant's destruction happens-before our construction.
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
            if (diff == 0) {
                // relaxed is enough: the CAS only arbitrates ownership of `pos`;
                // data visibility is carried by the cell's sequence number.
                if (enqueue_pos_.value.compare_exchange_weak(pos, pos + 1,
                                                             std::memory_order_relaxed,
                                                             std::memory_order_relaxed)) {
                    break;
                }
                backoff.pause();  // lost the race for `pos` to another producer
            } else if (diff < 0) {
                return false;  // cell still holds last lap's element: full
            } else {
                pos = enqueue_pos_.value.load(std::memory_order_relaxed);
            }
        }
        ::new (static_cast<void*>(cell->storage.bytes)) T(std::forward<Args>(args)...);
        // release: publishes the element to the consumer that claims `pos`.
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(value);
    }

    /// @p value is left untouched when the queue is full.
    [[nodiscard]] bool try_push(T&& value) noexcept {
        return try_emplace(std::move(value));
    }

    /// Moves the oldest element into @p out. Lock-free, any thread.
    /// @return false if the queue was observed empty.
    [[nodiscard]] bool try_pop(T& out) noexcept {
        std::size_t pos = 0;
        Cell* const cell = claim_for_pop(pos);
        if (cell == nullptr) {
            return false;
        }
        out = std::move(*cell->storage.ptr());
        release_after_pop(*cell, pos);
        return true;
    }

    /// Removes and returns the oldest element, or std::nullopt if empty.
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        std::size_t pos = 0;
        Cell* const cell = claim_for_pop(pos);
        if (cell == nullptr) {
            return std::nullopt;
        }
        std::optional<T> result{std::move(*cell->storage.ptr())};
        release_after_pop(*cell, pos);
        return result;
    }

    /// Snapshot of the element count; may be stale by the time it returns.
    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::size_t head = dequeue_pos_.value.load(std::memory_order_relaxed);
        const std::size_t tail = enqueue_pos_.value.load(std::memory_order_relaxed);
        const auto diff = static_cast<std::intptr_t>(tail) - static_cast<std::intptr_t>(head);
        return diff > 0 ? static_cast<std::size_t>(diff) : 0U;
    }

    [[nodiscard]] bool empty_approx() const noexcept { return size_approx() == 0; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    struct alignas(kCacheLineSize) Cell {
        std::atomic<std::size_t> sequence{0};
        detail::RawStorage<T> storage;
    };

    struct alignas(kCacheLineSize) PaddedPos {
        std::atomic<std::size_t> value{0};
    };

    [[nodiscard]] static std::size_t validated(std::size_t min_capacity) {
        if (min_capacity == 0) {
            throw std::invalid_argument("MpmcQueue capacity must be > 0");
        }
        // Capacity 1 would make "free for next lap" (pos + 1) indistinguishable
        // from "holds data" (pos + 1); 2 is the smallest correct size.
        return detail::round_up_pow2(min_capacity < 2 ? 2 : min_capacity);
    }

    /// Claims the cell at the dequeue position; returns nullptr if empty.
    [[nodiscard]] Cell* claim_for_pop(std::size_t& pos) noexcept {
        pos = dequeue_pos_.value.load(std::memory_order_relaxed);
        detail::Backoff backoff;
        for (;;) {
            Cell* const cell = &cells_[pos & mask_];
            // acquire: pairs with the producer's release store, making the
            // element constructed for `pos` visible before we read it.
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff =
                static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeue_pos_.value.compare_exchange_weak(pos, pos + 1,
                                                             std::memory_order_relaxed,
                                                             std::memory_order_relaxed)) {
                    return cell;
                }
                backoff.pause();  // lost the race for `pos` to another consumer
            } else if (diff < 0) {
                return nullptr;  // producer for `pos` has not published yet: empty
            } else {
                pos = dequeue_pos_.value.load(std::memory_order_relaxed);
            }
        }
    }

    void release_after_pop(Cell& cell, std::size_t pos) noexcept {
        cell.storage.ptr()->~T();
        // release: our read + destruction happen-before the producer of the
        // next lap (pos + capacity) constructs into this cell.
        cell.sequence.store(pos + capacity_, std::memory_order_release);
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    const std::unique_ptr<Cell[]> cells_;  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    PaddedPos enqueue_pos_;
    PaddedPos dequeue_pos_;
};

}  // namespace lfc
