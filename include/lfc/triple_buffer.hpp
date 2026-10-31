// SPDX-License-Identifier: MIT
/// @file triple_buffer.hpp
/// @brief Wait-free latest-value channel between one writer and one reader.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "lfc/config.hpp"

namespace lfc {

/// Classic triple buffer for "sensor-style" sharing: the writer publishes new
/// samples at its own rate, the reader always gets the most recent complete
/// sample, and neither side ever waits for the other (both are wait-free).
/// Intermediate samples are dropped by design.
///
/// Three buffers rotate between roles: *back* (owned by the writer), *front*
/// (owned by the reader) and *middle* (the hand-off slot). The middle index
/// and a "fresh" flag share one atomic byte, swapped with a single `exchange`.
///
/// Unlike Seqlock, `T` may be any copy/move-assignable type (e.g. one holding
/// a std::vector), because each side has exclusive access to its buffer.
template <typename T>
class TripleBuffer {
    static_assert(std::is_default_constructible_v<T>, "TripleBuffer requires a default-constructible T");

public:
    using value_type = T;

    TripleBuffer() = default;

    /// Initializes all three buffers with @p initial.
    explicit TripleBuffer(const T& initial) {
        for (auto& buffer : buffers_) {
            buffer.value = initial;
        }
    }

    TripleBuffer(const TripleBuffer&) = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;
    TripleBuffer(TripleBuffer&&) = delete;
    TripleBuffer& operator=(TripleBuffer&&) = delete;
    ~TripleBuffer() = default;

    // ----------------------------------------------------------------- writer

    /// Buffer the writer may fill in place before calling `publish()`.
    [[nodiscard]] T& write_buffer() noexcept { return buffers_[back_].value; }

    /// Makes the write buffer visible to the reader. Wait-free.
    void publish() noexcept {
        // acq_rel exchange:
        //  - release: our writes to the back buffer happen-before the reader's
        //    acquire exchange that picks this index up;
        //  - acquire: if the slot we receive was last released by the reader,
        //    its reads of that buffer happen-before we overwrite it.
        const std::uint8_t previous = middle_.exchange(static_cast<std::uint8_t>(back_ | kFresh),
                                                       std::memory_order_acq_rel);
        back_ = static_cast<std::uint8_t>(previous & kIndexMask);
    }

    /// Copies or moves @p value into the write buffer and publishes it.
    template <typename U>
    void write(U&& value) {
        write_buffer() = std::forward<U>(value);
        publish();
    }

    // ----------------------------------------------------------------- reader

    /// Swaps in the latest published buffer if there is one. Wait-free.
    /// @return true if a new value became visible since the previous call.
    bool update() noexcept {
        // relaxed peek: avoids an RMW (and cache-line ownership transfer) when
        // nothing new was published; the exchange below provides ordering.
        if ((middle_.load(std::memory_order_relaxed) & kFresh) == 0U) {
            return false;
        }
        // acq_rel exchange: acquire pairs with publish()'s release (data is
        // visible); release hands our old front buffer back to the writer.
        const std::uint8_t previous = middle_.exchange(front_, std::memory_order_acq_rel);
        front_ = static_cast<std::uint8_t>(previous & kIndexMask);
        return true;
    }

    /// Most recent value obtained by `update()`.
    [[nodiscard]] const T& read_buffer() const noexcept { return buffers_[front_].value; }

    /// Convenience: `update()` then `read_buffer()`.
    [[nodiscard]] const T& read() noexcept {
        update();
        return read_buffer();
    }

    /// True if a value was published that the reader has not swapped in yet.
    [[nodiscard]] bool has_fresh() const noexcept {
        return (middle_.load(std::memory_order_relaxed) & kFresh) != 0U;
    }

private:
    static constexpr std::uint8_t kIndexMask = 0x03U;
    static constexpr std::uint8_t kFresh = 0x04U;

    struct alignas(kCacheLineSize) Buffer {
        T value{};
    };

    std::array<Buffer, 3> buffers_{};
    alignas(kCacheLineSize) std::uint8_t back_{0};    // writer-private
    alignas(kCacheLineSize) std::atomic<std::uint8_t> middle_{1};
    alignas(kCacheLineSize) std::uint8_t front_{2};   // reader-private
};

}  // namespace lfc
