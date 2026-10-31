// SPDX-License-Identifier: MIT
// Small-history linearizability checker (Wing & Gong search with memoization).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <utility>
#include <vector>

namespace lfc_test {

enum class OpKind : std::uint8_t { Push, Pop };

/// One completed operation with its real-time interval [invoke, response].
struct Op {
    OpKind kind = OpKind::Push;
    int value = 0;     // value pushed, or value returned by a successful pop
    bool ok = false;   // push accepted / pop returned a value
    std::uint64_t invoke = 0;
    std::uint64_t response = 0;
};

/// Logical clock: every invoke/response takes a unique, totally ordered tick.
/// Because each tick is a seq_cst RMW sequenced around the real operation,
/// `a.response < b.invoke` implies `a` really finished before `b` started.
class HistoryClock {
public:
    [[nodiscard]] std::uint64_t tick() noexcept { return now_.fetch_add(1, std::memory_order_seq_cst); }

private:
    std::atomic<std::uint64_t> now_{0};
};

/// Sequential FIFO specification with optional capacity.
class FifoModel {
public:
    explicit FifoModel(std::size_t capacity) : capacity_(capacity) {}

    [[nodiscard]] bool apply(const Op& op) {
        if (op.kind == OpKind::Push) {
            if (!op.ok) {
                return items_.size() >= capacity_;
            }
            if (items_.size() >= capacity_) {
                return false;
            }
            items_.push_back(op.value);
            return true;
        }
        if (!op.ok) {
            return items_.empty();
        }
        if (items_.empty() || items_.front() != op.value) {
            return false;
        }
        items_.pop_front();
        return true;
    }

    [[nodiscard]] std::vector<int> key() const { return {items_.begin(), items_.end()}; }

private:
    std::size_t capacity_;
    std::deque<int> items_;
};

/// Sequential LIFO specification with optional capacity.
class LifoModel {
public:
    explicit LifoModel(std::size_t capacity) : capacity_(capacity) {}

    [[nodiscard]] bool apply(const Op& op) {
        if (op.kind == OpKind::Push) {
            if (!op.ok) {
                return items_.size() >= capacity_;
            }
            if (items_.size() >= capacity_) {
                return false;
            }
            items_.push_back(op.value);
            return true;
        }
        if (!op.ok) {
            return items_.empty();
        }
        if (items_.empty() || items_.back() != op.value) {
            return false;
        }
        items_.pop_back();
        return true;
    }

    [[nodiscard]] std::vector<int> key() const { return items_; }

private:
    std::size_t capacity_;
    std::vector<int> items_;
};

/// Returns true if some total order of @p history that respects real-time
/// precedence is a legal sequential execution of @p model. Supports up to 64
/// operations; intended for histories of roughly 8-16 ops.
template <typename Model>
class LinearizabilityChecker {
public:
    LinearizabilityChecker(std::vector<Op> history, Model initial)
        : history_(std::move(history)), initial_(std::move(initial)) {}

    [[nodiscard]] bool check() {
        failed_.clear();
        return search(0, initial_);
    }

private:
    using Mask = std::uint64_t;

    [[nodiscard]] bool search(Mask done, const Model& state) {
        const std::size_t n = history_.size();
        if (done == full_mask(n)) {
            return true;
        }
        if (failed_.count({done, state.key()}) != 0) {
            return false;
        }
        // Earliest response among pending ops: any op invoked after it cannot
        // be linearized next, because that earlier op must come first.
        std::uint64_t min_response = UINT64_MAX;
        for (std::size_t i = 0; i < n; ++i) {
            if ((done & bit(i)) == 0 && history_[i].response < min_response) {
                min_response = history_[i].response;
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            if ((done & bit(i)) != 0 || history_[i].invoke > min_response) {
                continue;
            }
            Model next = state;
            if (next.apply(history_[i]) && search(done | bit(i), next)) {
                return true;
            }
        }
        failed_.insert({done, state.key()});
        return false;
    }

    [[nodiscard]] static constexpr Mask bit(std::size_t i) noexcept { return Mask{1} << i; }
    [[nodiscard]] static constexpr Mask full_mask(std::size_t n) noexcept {
        return n >= 64 ? ~Mask{0} : (Mask{1} << n) - 1;
    }

    std::vector<Op> history_;
    Model initial_;
    std::set<std::pair<Mask, std::vector<int>>> failed_;
};

}  // namespace lfc_test
