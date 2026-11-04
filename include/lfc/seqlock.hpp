// SPDX-License-Identifier: MIT
/// @file seqlock.hpp
/// @brief Single-writer sequence lock for small trivially copyable values.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

#include "lfc/config.hpp"

namespace lfc {

/// Publishes a trivially copyable value from one writer to any number of
/// readers. The writer never waits (wait-free); readers never block the writer
/// and simply retry if they overlap a write (lock-free for readers).
///
/// The payload is stored as an array of `std::atomic<uint64_t>` words rather
/// than a plain `T`, following H.-J. Boehm, "Can Seqlocks Get Along With
/// Programming Language Memory Models?" (2012): a torn read is still detected
/// and discarded via the sequence counter, but there is no data race in the
/// C++ memory model, so the compiler cannot miscompile the speculative read.
/// Ordering uses release payload stores / acquire payload loads instead of
/// standalone fences; on x86-64 these compile to plain moves, and unlike
/// fences they are fully understood by ThreadSanitizer.
template <typename T>
class Seqlock {
    static_assert(std::is_trivially_copyable_v<T>, "Seqlock requires a trivially copyable T");
    static_assert(std::is_default_constructible_v<T>, "Seqlock requires a default-constructible T");

public:
    using value_type = T;

    Seqlock() noexcept : Seqlock(T{}) {}
    explicit Seqlock(const T& initial) noexcept { store_words(initial, std::memory_order_relaxed); }

    Seqlock(const Seqlock&) = delete;
    Seqlock& operator=(const Seqlock&) = delete;
    Seqlock(Seqlock&&) = delete;
    Seqlock& operator=(Seqlock&&) = delete;
    ~Seqlock() = default;

    /// Publishes @p value. Wait-free. Single writer only.
    void store(const T& value) noexcept {
        const std::uint64_t seq = sequence_.load(std::memory_order_relaxed);
        // Odd sequence = write in progress. relaxed is enough: the release
        // stores of the payload words below cannot be reordered before it.
        sequence_.store(seq + 1, std::memory_order_relaxed);
        // Payload words are stored with release: a reader that acquires any
        // new word is guaranteed to also see the odd sequence store above, so
        // its re-check of the sequence detects the overlap.
        store_words(value, std::memory_order_release);
        // release: all payload stores happen-before a reader that acquires seq + 2.
        sequence_.store(seq + 2, std::memory_order_release);
    }

    /// Single read attempt. Returns std::nullopt if it raced with a write.
    [[nodiscard]] std::optional<T> try_load() const noexcept {
        // acquire: pairs with the writer's final release store, so if we see an
        // even value the matching payload is visible.
        const std::uint64_t before = sequence_.load(std::memory_order_acquire);
        if ((before & 1U) != 0U) {
            return std::nullopt;
        }
        // acquire loads keep the re-check below from moving above them, and
        // pair with the writer's release payload stores (see store()).
        T value = load_words();
        const std::uint64_t after = sequence_.load(std::memory_order_relaxed);
        if (before != after) {
            return std::nullopt;
        }
        return value;
    }

    /// Reads a consistent snapshot, retrying while a write is in progress.
    /// Lock-free: only fails to make progress while the writer keeps writing.
    [[nodiscard]] T load() const noexcept {
        for (;;) {
            if (auto value = try_load()) {
                return *value;
            }
        }
    }

    /// Number of completed writes since construction.
    [[nodiscard]] std::uint64_t version() const noexcept {
        return sequence_.load(std::memory_order_acquire) / 2U;
    }

private:
    static constexpr std::size_t kWordCount = (sizeof(T) + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);
    using Words = std::array<std::uint64_t, kWordCount>;

    void store_words(const T& value, std::memory_order order) noexcept {
        Words words{};
        std::memcpy(words.data(), &value, sizeof(T));
        for (std::size_t i = 0; i < kWordCount; ++i) {
            words_[i].store(words[i], order);
        }
    }

    [[nodiscard]] T load_words() const noexcept {
        Words words{};
        for (std::size_t i = 0; i < kWordCount; ++i) {
            words[i] = words_[i].load(std::memory_order_acquire);
        }
        T value{};
        // The void* cast documents that T is trivially copyable (checked above).
        std::memcpy(static_cast<void*>(&value), words.data(), sizeof(T));
        return value;
    }

    alignas(kCacheLineSize) std::atomic<std::uint64_t> sequence_{0};
    std::array<std::atomic<std::uint64_t>, kWordCount> words_{};
};

}  // namespace lfc
