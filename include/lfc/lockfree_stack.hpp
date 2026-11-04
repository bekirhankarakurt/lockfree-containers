// SPDX-License-Identifier: MIT
/// @file lockfree_stack.hpp
/// @brief Bounded lock-free LIFO stack with ABA protection via tagged indices.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "lfc/config.hpp"

namespace lfc {

/// Bounded Treiber stack backed by a pre-allocated node pool.
///
/// **ABA protection.** The head is a 64-bit tagged pointer: a 32-bit node index
/// plus a 32-bit modification counter, updated with a single-width CAS (always
/// lock-free, no `cmpxchg16b`/libatomic needed). Every successful CAS bumps the
/// tag, so a thread that read `{A, tag}`, stalled while `A` was popped and
/// pushed back, will fail its CAS because the tag moved on.
///
/// **Memory reclamation.** Nodes live in a fixed array for the lifetime of the
/// stack and are recycled through a second tagged free list, so a stale reader
/// can never dereference freed memory -- no hazard pointers or epochs needed.
/// Nothing is allocated after construction, which suits real-time systems.
template <typename T>
class LockFreeStack {
    static_assert(std::is_nothrow_destructible_v<T>, "T must be nothrow destructible");
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move constructible and assignable");

public:
    using value_type = T;

    /// @param capacity Maximum number of elements (1 .. 2^32 - 2).
    /// @throws std::invalid_argument if @p capacity is out of range.
    explicit LockFreeStack(std::size_t capacity)
        : capacity_(validated(capacity)), nodes_(capacity_) {
        // Thread all nodes onto the free list: 0 -> 1 -> ... -> capacity-1.
        for (std::size_t i = 0; i + 1 < capacity_; ++i) {
            nodes_[i].next.store(static_cast<Index>(i + 1), std::memory_order_relaxed);
        }
        nodes_[capacity_ - 1].next.store(kNull, std::memory_order_relaxed);
        free_.head.store(pack(0, 0), std::memory_order_relaxed);
        data_.head.store(pack(kNull, 0), std::memory_order_relaxed);
    }

    LockFreeStack(const LockFreeStack&) = delete;
    LockFreeStack& operator=(const LockFreeStack&) = delete;
    LockFreeStack(LockFreeStack&&) = delete;
    LockFreeStack& operator=(LockFreeStack&&) = delete;

    ~LockFreeStack() {
        Index index = index_of(data_.head.load(std::memory_order_relaxed));
        while (index != kNull) {
            nodes_[index].storage.ptr()->~T();
            index = nodes_[index].next.load(std::memory_order_relaxed);
        }
    }

    /// Constructs an element on top of the stack. Lock-free, any thread.
    /// @return false if all nodes are in use.
    template <typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept(
        std::is_nothrow_constructible_v<T, Args&&...>) {
        const Index index = pop_node(free_);
        if (index == kNull) {
            return false;
        }
        // If T's constructor throws, the guard returns the node to the pool.
        NodeGuard guard{*this, index};
        ::new (nodes_[index].storage.raw()) T(std::forward<Args>(args)...);
        guard.release();
        push_node(data_, index);
        return true;
    }

    [[nodiscard]] bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(value);
    }

    /// @p value is left untouched when the stack is full.
    [[nodiscard]] bool try_push(T&& value) noexcept { return try_emplace(std::move(value)); }

    /// Moves the top element into @p out. Lock-free, any thread.
    /// @return false if the stack is empty.
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const Index index = pop_node(data_);
        if (index == kNull) {
            return false;
        }
        T* const element = nodes_[index].storage.ptr();
        out = std::move(*element);
        element->~T();
        push_node(free_, index);
        return true;
    }

    /// Removes and returns the top element, or std::nullopt if empty.
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        const Index index = pop_node(data_);
        if (index == kNull) {
            return std::nullopt;
        }
        T* const element = nodes_[index].storage.ptr();
        std::optional<T> result{std::move(*element)};
        element->~T();
        push_node(free_, index);
        return result;
    }

    /// Snapshot emptiness check; may be stale by the time it returns.
    [[nodiscard]] bool empty_approx() const noexcept {
        return index_of(data_.head.load(std::memory_order_acquire)) == kNull;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// True when the 64-bit head CAS is natively lock-free on this platform.
    [[nodiscard]] static constexpr bool is_always_lock_free() noexcept {
        return std::atomic<std::uint64_t>::is_always_lock_free;
    }

private:
    using Index = std::uint32_t;
    using Tagged = std::uint64_t;  // [ tag : 32 | index : 32 ]

    static constexpr Index kNull = std::numeric_limits<Index>::max();

    struct Node {
        // Atomic because a stale popper may read `next` of a node that another
        // thread is concurrently relinking; the tag CAS discards that value.
        std::atomic<Index> next{kNull};
        detail::RawStorage<T> storage;
    };

    /// RAII: gives a free-list node back unless release() is called.
    class NodeGuard {
    public:
        NodeGuard(LockFreeStack& owner, Index index) noexcept : owner_(owner), index_(index) {}
        NodeGuard(const NodeGuard&) = delete;
        NodeGuard& operator=(const NodeGuard&) = delete;
        NodeGuard(NodeGuard&&) = delete;
        NodeGuard& operator=(NodeGuard&&) = delete;
        ~NodeGuard() {
            if (armed_) {
                owner_.push_node(owner_.free_, index_);
            }
        }
        void release() noexcept { armed_ = false; }

    private:
        LockFreeStack& owner_;
        Index index_;
        bool armed_ = true;
    };

    struct alignas(kCacheLineSize) Head {
        std::atomic<Tagged> head{0};
    };

    [[nodiscard]] static constexpr Tagged pack(Index index, std::uint32_t tag) noexcept {
        return (static_cast<Tagged>(tag) << 32U) | index;
    }
    [[nodiscard]] static constexpr Index index_of(Tagged tagged) noexcept {
        return static_cast<Index>(tagged & 0xFFFF'FFFFU);
    }
    [[nodiscard]] static constexpr std::uint32_t tag_of(Tagged tagged) noexcept {
        return static_cast<std::uint32_t>(tagged >> 32U);
    }

    [[nodiscard]] static std::size_t validated(std::size_t capacity) {
        if (capacity == 0 || capacity >= kNull) {
            throw std::invalid_argument("LockFreeStack capacity must be in [1, 2^32 - 2]");
        }
        return capacity;
    }

    /// Treiber pop of one node index from @p list; kNull if the list is empty.
    [[nodiscard]] Index pop_node(Head& list) noexcept {
        // acquire: pairs with the release CAS in push_node so that the node's
        // `next` link and (for the data list) its element are visible.
        Tagged head = list.head.load(std::memory_order_acquire);
        detail::Backoff backoff;
        for (;;) {
            const Index index = index_of(head);
            if (index == kNull) {
                return kNull;
            }
            // May be stale if `index` was popped concurrently; then the tag has
            // changed and the CAS below fails, so the stale value is never used.
            const Index next = nodes_[index].next.load(std::memory_order_relaxed);
            const Tagged desired = pack(next, tag_of(head) + 1U);
            // success acquire: we now own the node and must see what the pusher
            // wrote. failure acquire: reload head for the next attempt.
            if (list.head.compare_exchange_weak(head, desired, std::memory_order_acquire,
                                                std::memory_order_acquire)) {
                return index;
            }
            backoff.pause();
        }
    }

    /// Treiber push of node @p index onto @p list.
    void push_node(Head& list, Index index) noexcept {
        Tagged head = list.head.load(std::memory_order_relaxed);
        detail::Backoff backoff;
        for (;;) {
            nodes_[index].next.store(index_of(head), std::memory_order_relaxed);
            const Tagged desired = pack(index, tag_of(head) + 1U);
            // release: publishes the `next` link and the element constructed in
            // the node to whichever thread pops it.
            if (list.head.compare_exchange_weak(head, desired, std::memory_order_release,
                                                std::memory_order_relaxed)) {
                return;
            }
            backoff.pause();
        }
    }

    const std::size_t capacity_;
    std::vector<Node> nodes_;
    Head data_;
    Head free_;
};

}  // namespace lfc
