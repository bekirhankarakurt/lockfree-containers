// SPDX-License-Identifier: MIT
/// @file spsc_ring.hpp
/// @brief Wait-free single-producer / single-consumer bounded ring buffer.
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "lfc/config.hpp"

namespace lfc {

/// Bounded FIFO for exactly one producer thread and one consumer thread.
///
/// Both `try_push` and `try_pop` are wait-free: each completes in a bounded
/// number of steps with no CAS loops. The capacity is rounded up to a power of
/// two so that index wrap-around is a mask instead of a division.
///
/// Each side keeps a private cached copy of the other side's index, so the
/// shared cache line is only touched when the cached value says the ring is
/// full (producer) or empty (consumer).
template <typename T>
class SpscRing {
    static_assert(std::is_nothrow_destructible_v<T>, "T must be nothrow destructible");

public:
    using value_type = T;

    /// @param min_capacity Minimum number of elements; rounded up to a power of two.
    /// @throws std::invalid_argument if @p min_capacity is zero.
    explicit SpscRing(std::size_t min_capacity)
        : capacity_(validated(min_capacity)),
          mask_(capacity_ - 1),
          slots_(capacity_) {}

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;
    SpscRing(SpscRing&&) = delete;
    SpscRing& operator=(SpscRing&&) = delete;

    ~SpscRing() {
        // No concurrent access is allowed during destruction.
        std::size_t head = consumer_.index.load(std::memory_order_relaxed);
        const std::size_t tail = producer_.index.load(std::memory_order_relaxed);
        for (; head != tail; ++head) {
            slots_[head & mask_].ptr()->~T();
        }
    }

    /// Constructs an element in place. Producer thread only.
    /// @return false if the ring is full (arguments are not consumed).
    template <typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept(
        std::is_nothrow_constructible_v<T, Args&&...>) {
        const std::size_t tail = producer_.index.load(std::memory_order_relaxed);
        if (tail - producer_.cached_other == capacity_) {
            // acquire: pairs with the consumer's release store of its index, so
            // the consumer's move-out + destructor of that slot happen-before
            // we construct a new object in it.
            producer_.cached_other = consumer_.index.load(std::memory_order_acquire);
            if (tail - producer_.cached_other == capacity_) {
                return false;
            }
        }
        ::new (slots_[tail & mask_].raw()) T(std::forward<Args>(args)...);
        // release: publishes the constructed element; pairs with the consumer's
        // acquire load of producer_.index.
        producer_.index.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// Copies @p value into the ring. Producer thread only.
    [[nodiscard]] bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(value);
    }

    /// Moves @p value into the ring. Producer thread only.
    /// @p value is left untouched when the ring is full.
    [[nodiscard]] bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return try_emplace(std::move(value));
    }

    /// Moves the oldest element into @p out. Consumer thread only.
    /// @return false if the ring is empty.
    [[nodiscard]] bool try_pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
        T* const element = front();
        if (element == nullptr) {
            return false;
        }
        out = std::move(*element);
        pop_front();
        return true;
    }

    /// Removes and returns the oldest element. Consumer thread only.
    [[nodiscard]] std::optional<T> try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        T* const element = front();
        if (element == nullptr) {
            return std::nullopt;
        }
        std::optional<T> result{std::move(*element)};
        pop_front();
        return result;
    }

    /// Returns a pointer to the oldest element, or nullptr if empty.
    /// The element stays in the ring until `pop_front()`. Consumer thread only.
    [[nodiscard]] T* front() noexcept {
        const std::size_t head = consumer_.index.load(std::memory_order_relaxed);
        if (head == consumer_.cached_other) {
            // acquire: pairs with the producer's release store so the element
            // constructed in slot `head` is fully visible.
            consumer_.cached_other = producer_.index.load(std::memory_order_acquire);
            if (head == consumer_.cached_other) {
                return nullptr;
            }
        }
        return slots_[head & mask_].ptr();
    }

    /// Destroys the oldest element. Precondition: `front() != nullptr`.
    /// Consumer thread only.
    void pop_front() noexcept {
        const std::size_t head = consumer_.index.load(std::memory_order_relaxed);
        slots_[head & mask_].ptr()->~T();
        // release: our reads of the slot and its destruction happen-before the
        // producer reuses it (pairs with the producer's acquire load).
        consumer_.index.store(head + 1, std::memory_order_release);
    }

    /// Number of elements; exact only when called from the producer or consumer
    /// while the other side is idle.
    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::size_t tail = producer_.index.load(std::memory_order_acquire);
        const std::size_t head = consumer_.index.load(std::memory_order_acquire);
        return tail - head;  // unsigned wrap-around keeps this correct
    }

    [[nodiscard]] bool empty_approx() const noexcept { return size_approx() == 0; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    [[nodiscard]] static std::size_t validated(std::size_t min_capacity) {
        if (min_capacity == 0) {
            throw std::invalid_argument("SpscRing capacity must be > 0");
        }
        return detail::round_up_pow2(min_capacity);
    }

    /// One side's state: its own index (shared) and a cache of the other side's
    /// index (private). Aligned to keep producer and consumer on separate lines.
    struct alignas(kCacheLineSize) Side {
        std::atomic<std::size_t> index{0};
        std::size_t cached_other{0};
    };

    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<detail::RawStorage<T>> slots_;
    Side producer_;
    Side consumer_;
};

}  // namespace lfc
