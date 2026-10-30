// SPDX-License-Identifier: MIT
/// @file config.hpp
/// @brief Shared configuration: cache-line size and padding helpers.
#pragma once

#include <cstddef>
#include <new>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace lfc {

/// Size, in bytes, used to keep independently written atomics on separate
/// cache lines (avoids false sharing).
///
/// Resolution order:
///  1. `LFC_CACHE_LINE_SIZE` if the user defines it (e.g. 128 for Apple M-series
///     or to defeat the adjacent-line prefetcher on x86).
///  2. `std::hardware_destructive_interference_size` when the standard library
///     provides it and the compiler does not warn about its ABI instability
///     (GCC emits -Winterference-size for every use in a header).
///  3. 64 bytes, correct for virtually all x86-64 and ARMv8 cores.
#if defined(LFC_CACHE_LINE_SIZE)
inline constexpr std::size_t kCacheLineSize = LFC_CACHE_LINE_SIZE;
#elif defined(__cpp_lib_hardware_interference_size) && \
    (defined(__clang__) || !defined(__GNUC__))
inline constexpr std::size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLineSize = 64;
#endif

static_assert((kCacheLineSize & (kCacheLineSize - 1)) == 0,
              "cache line size must be a power of two");

namespace detail {

/// Rounds @p value up to the next power of two (minimum 1).
[[nodiscard]] constexpr std::size_t round_up_pow2(std::size_t value) noexcept {
    std::size_t result = 1;
    while (result < value) {
        result <<= 1U;
    }
    return result;
}

/// Raw, correctly aligned storage for one `T` whose lifetime is managed
/// manually with placement new / explicit destructor calls.
template <typename T>
struct alignas(T) RawStorage {
    std::byte bytes[sizeof(T)];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

    [[nodiscard]] T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(bytes)); }  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    [[nodiscard]] const T* ptr() const noexcept {
        return std::launder(reinterpret_cast<const T*>(bytes));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    }
};

/// Hints the CPU that we are spinning (frees pipeline resources for the
/// sibling hyper-thread and lowers power). No-op on unknown architectures.
inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
    asm volatile("yield" ::: "memory");
#endif
}

/// Bounded exponential backoff for CAS retry loops. Under contention, backing
/// off after a failed CAS lets the current owner of the cache line finish,
/// which raises total throughput. The cap keeps worst-case delay small.
class Backoff {
public:
    void pause() noexcept {
        for (unsigned i = 0; i < spins_; ++i) {
            cpu_relax();
        }
        if (spins_ < kMaxSpins) {
            spins_ <<= 1U;
        }
    }

private:
    static constexpr unsigned kMaxSpins = 64;
    unsigned spins_ = 1;
};

}  // namespace detail
}  // namespace lfc
