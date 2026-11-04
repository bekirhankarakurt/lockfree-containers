// SPDX-License-Identifier: MIT
// Helpers shared by the concurrency tests.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <thread>

namespace lfc_test {

/// Scales iteration counts down under sanitizers / via LFC_TEST_SCALE.
inline std::size_t scaled(std::size_t iterations) {
    std::size_t divisor = 1;
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    divisor = 10;
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    divisor = 10;
#endif
#endif
    const std::size_t result = iterations / divisor;
    return result == 0 ? 1 : result;
}

/// Spin barrier so all worker threads start hammering at the same moment.
class StartGate {
public:
    explicit StartGate(std::size_t participants) : remaining_(participants) {}

    void arrive_and_wait() noexcept {
        remaining_.fetch_sub(1, std::memory_order_acq_rel);
        while (remaining_.load(std::memory_order_acquire) != 0) {
            std::this_thread::yield();
        }
    }

private:
    std::atomic<std::size_t> remaining_;
};

/// Counts live instances to catch leaks and double destruction.
struct Tracked {
    static std::atomic<long>& live() {
        static std::atomic<long> count{0};
        return count;
    }

    explicit Tracked(int v = 0) noexcept : value(v) { live().fetch_add(1, std::memory_order_relaxed); }
    Tracked(const Tracked& other) noexcept : value(other.value) { live().fetch_add(1, std::memory_order_relaxed); }
    Tracked(Tracked&& other) noexcept : value(other.value) { live().fetch_add(1, std::memory_order_relaxed); }
    Tracked& operator=(const Tracked&) noexcept = default;
    Tracked& operator=(Tracked&&) noexcept = default;
    ~Tracked() { live().fetch_sub(1, std::memory_order_relaxed); }

    int value;
};

/// Move-only payload; a moved-from instance holds -1.
struct MoveOnly {
    explicit MoveOnly(int v = 0) noexcept : value(v) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) noexcept : value(other.value) { other.value = -1; }
    MoveOnly& operator=(MoveOnly&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
    ~MoveOnly() = default;

    int value;
};

}  // namespace lfc_test
