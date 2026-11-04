// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "lfc/spsc_ring.hpp"
#include "test_framework.hpp"
#include "test_support.hpp"

using lfc::SpscRing;
using lfc_test::Tracked;

TEST_CASE("spsc: capacity rounds up to power of two") {
    const SpscRing<int> a(1);
    const SpscRing<int> b(5);
    const SpscRing<int> c(64);
    CHECK(a.capacity() == 1);
    CHECK(b.capacity() == 8);
    CHECK(c.capacity() == 64);
    CHECK_THROWS_AS(SpscRing<int>(0), std::invalid_argument);
}

TEST_CASE("spsc: fifo order, full and empty") {
    SpscRing<int> ring(4);
    CHECK(ring.empty_approx());
    for (int i = 0; i < 4; ++i) {
        CHECK(ring.try_push(i));
    }
    CHECK(!ring.try_push(99));
    CHECK(ring.size_approx() == 4);
    for (int i = 0; i < 4; ++i) {
        int out = -1;
        CHECK(ring.try_pop(out));
        CHECK(out == i);
    }
    int out = -1;
    CHECK(!ring.try_pop(out));
    CHECK(!ring.try_pop().has_value());
}

TEST_CASE("spsc: wrap-around over many laps") {
    SpscRing<std::uint32_t> ring(8);
    std::uint32_t next_pop = 0;
    for (std::uint32_t i = 0; i < 1000; ++i) {
        REQUIRE(ring.try_push(i));
        if (i % 3 == 2) {
            while (auto v = ring.try_pop()) {
                CHECK(*v == next_pop);
                ++next_pop;
            }
        }
    }
    while (auto v = ring.try_pop()) {
        CHECK(*v == next_pop);
        ++next_pop;
    }
    CHECK(next_pop == 1000);
}

TEST_CASE("spsc: front/pop_front peek interface") {
    SpscRing<std::string> ring(2);
    CHECK(ring.front() == nullptr);
    CHECK(ring.try_emplace(3U, 'x'));
    std::string* const head = ring.front();
    REQUIRE(head != nullptr);
    CHECK(*head == "xxx");
    ring.pop_front();
    CHECK(ring.front() == nullptr);
}

TEST_CASE("spsc: move-only types and failed push keeps value") {
    SpscRing<std::unique_ptr<int>> ring(1);
    CHECK(ring.try_push(std::make_unique<int>(1)));
    auto second = std::make_unique<int>(2);
    CHECK(!ring.try_push(std::move(second)));
    CHECK(second != nullptr);  // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved)
    auto out = ring.try_pop();
    REQUIRE(out.has_value());
    CHECK(**out == 1);
}

TEST_CASE("spsc: destructor destroys remaining elements") {
    {
        SpscRing<Tracked> ring(8);
        for (int i = 0; i < 5; ++i) {
            CHECK(ring.try_emplace(i));
        }
        CHECK(ring.try_pop().has_value());
        CHECK(Tracked::live().load() == 4);
    }
    CHECK(Tracked::live().load() == 0);
}

TEST_CASE("spsc: concurrent stress preserves order and content") {
    const std::uint64_t count = lfc_test::scaled(2'000'000);
    SpscRing<std::uint64_t> ring(1024);
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i) {
            while (!ring.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });
    std::uint64_t expected = 0;
    bool ordered = true;
    while (expected < count) {
        std::uint64_t value = 0;
        if (ring.try_pop(value)) {
            ordered = ordered && value == expected;
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    CHECK(ordered);
    CHECK(ring.empty_approx());
}

LFC_TEST_MAIN()
