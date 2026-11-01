// SPDX-License-Identifier: MIT
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include "lfc/lockfree_stack.hpp"
#include "test_framework.hpp"
#include "test_support.hpp"

using lfc::LockFreeStack;
using lfc_test::MoveOnly;
using lfc_test::Tracked;

TEST_CASE("stack: lifo order, full and empty") {
    LockFreeStack<int> stack(3);
    CHECK(stack.capacity() == 3);
    CHECK(stack.empty_approx());
    CHECK(stack.try_push(1));
    CHECK(stack.try_push(2));
    CHECK(stack.try_push(3));
    CHECK(!stack.try_push(4));
    CHECK(stack.try_pop() == 3);
    CHECK(stack.try_pop() == 2);
    CHECK(stack.try_push(5));
    CHECK(stack.try_pop() == 5);
    CHECK(stack.try_pop() == 1);
    CHECK(!stack.try_pop().has_value());
    CHECK(stack.empty_approx());
    CHECK_THROWS_AS(LockFreeStack<int>(0), std::invalid_argument);
    CHECK(LockFreeStack<int>::is_always_lock_free());
}

TEST_CASE("stack: move-only and destructor cleanup") {
    {
        LockFreeStack<Tracked> stack(4);
        CHECK(stack.try_emplace(1));
        CHECK(stack.try_emplace(2));
        Tracked out;
        CHECK(stack.try_pop(out));
        CHECK(out.value == 2);
        CHECK(Tracked::live().load() == 2);  // one in the stack + `out`
    }
    CHECK(Tracked::live().load() == 0);
    LockFreeStack<MoveOnly> moves(2);
    CHECK(moves.try_push(MoveOnly{9}));
    const auto popped = moves.try_pop();
    REQUIRE(popped.has_value());
    CHECK(popped->value == 9);
}

namespace {

struct ThrowsOnCopy {
    ThrowsOnCopy() = default;
    ThrowsOnCopy(const ThrowsOnCopy& /*other*/) { throw std::runtime_error("copy"); }
    ThrowsOnCopy(ThrowsOnCopy&&) noexcept = default;
    ThrowsOnCopy& operator=(const ThrowsOnCopy&) = default;
    ThrowsOnCopy& operator=(ThrowsOnCopy&&) noexcept = default;
    ~ThrowsOnCopy() = default;
};

}  // namespace

TEST_CASE("stack: throwing constructor returns node to the pool") {
    LockFreeStack<ThrowsOnCopy> stack(1);
    const ThrowsOnCopy source;
    CHECK_THROWS_AS((void)stack.try_push(source), std::runtime_error);
    CHECK(stack.try_push(ThrowsOnCopy{}));  // node was recycled
}

/// Hammers push/pop from many threads on a small pool so that nodes are
/// recycled constantly -- the scenario where an unprotected Treiber stack hits
/// ABA. Every value must come out exactly once.
TEST_CASE("stack: concurrent stress with node recycling (ABA)") {
    constexpr unsigned kThreads = 4;
    const std::uint64_t per_thread = lfc_test::scaled(200'000);
    LockFreeStack<std::uint64_t> stack(8);
    const std::uint64_t total = per_thread * kThreads;
    std::vector<std::atomic<std::uint8_t>> seen(total);
    std::atomic<std::uint64_t> popped_total{0};
    lfc_test::StartGate gate(kThreads);

    std::vector<std::thread> threads;
    for (unsigned t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            gate.arrive_and_wait();
            std::uint64_t pushed = 0;
            while (pushed < per_thread || popped_total.load(std::memory_order_relaxed) < total) {
                if (pushed < per_thread && stack.try_push(t * per_thread + pushed)) {
                    ++pushed;
                }
                std::uint64_t value = 0;
                if (stack.try_pop(value)) {
                    seen[value].fetch_add(1, std::memory_order_relaxed);
                    popped_total.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    bool exactly_once = true;
    for (const auto& s : seen) {
        exactly_once = exactly_once && s.load() == 1;
    }
    CHECK(exactly_once);
    CHECK(stack.empty_approx());
}

LFC_TEST_MAIN()
