// SPDX-License-Identifier: MIT
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "lfc/mpmc_queue.hpp"
#include "test_framework.hpp"
#include "test_support.hpp"

using lfc::MpmcQueue;
using lfc_test::Tracked;

TEST_CASE("mpmc: capacity rules") {
    const MpmcQueue<int> a(1);
    const MpmcQueue<int> b(100);
    CHECK(a.capacity() == 2);
    CHECK(b.capacity() == 128);
    CHECK_THROWS_AS(MpmcQueue<int>(0), std::invalid_argument);
}

TEST_CASE("mpmc: single-threaded fifo, full, empty, wrap") {
    MpmcQueue<int> queue(4);
    for (int lap = 0; lap < 10; ++lap) {
        for (int i = 0; i < 4; ++i) {
            CHECK(queue.try_push(lap * 10 + i));
        }
        CHECK(!queue.try_push(-1));
        CHECK(queue.size_approx() == 4);
        for (int i = 0; i < 4; ++i) {
            int out = -1;
            CHECK(queue.try_pop(out));
            CHECK(out == lap * 10 + i);
        }
        CHECK(!queue.try_pop().has_value());
    }
}

TEST_CASE("mpmc: move-only payload") {
    MpmcQueue<std::unique_ptr<int>> queue(2);
    CHECK(queue.try_emplace(std::make_unique<int>(7)));
    auto out = queue.try_pop();
    REQUIRE(out.has_value());
    CHECK(**out == 7);
}

TEST_CASE("mpmc: destructor destroys remaining elements") {
    {
        MpmcQueue<Tracked> queue(8);
        for (int i = 0; i < 6; ++i) {
            CHECK(queue.try_emplace(i));
        }
        Tracked out;
        CHECK(queue.try_pop(out));
        CHECK(Tracked::live().load() == 6);  // 5 queued + `out`
    }
    CHECK(Tracked::live().load() == 0);
}

namespace {

/// N producers each push a disjoint, increasing range; M consumers pop.
/// Verifies every value is delivered exactly once and that values from the
/// same producer arrive in order at each consumer (per-producer FIFO).
void run_stress(unsigned producers, unsigned consumers, std::size_t capacity) {
    const std::uint64_t per_producer = lfc_test::scaled(200'000);
    const std::uint64_t total = per_producer * producers;
    MpmcQueue<std::uint64_t> queue(capacity);
    std::vector<std::atomic<std::uint8_t>> seen(total);
    std::atomic<std::uint64_t> popped{0};
    std::atomic<bool> order_ok{true};
    lfc_test::StartGate gate(producers + consumers);

    std::vector<std::thread> threads;
    for (unsigned p = 0; p < producers; ++p) {
        threads.emplace_back([&, p] {
            gate.arrive_and_wait();
            for (std::uint64_t i = 0; i < per_producer; ++i) {
                const std::uint64_t value = (static_cast<std::uint64_t>(p) << 40U) | i;
                while (!queue.try_push(value)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (unsigned c = 0; c < consumers; ++c) {
        threads.emplace_back([&] {
            gate.arrive_and_wait();
            std::vector<std::int64_t> last(producers, -1);
            while (popped.load(std::memory_order_relaxed) < total) {
                std::uint64_t value = 0;
                if (!queue.try_pop(value)) {
                    std::this_thread::yield();
                    continue;
                }
                popped.fetch_add(1, std::memory_order_relaxed);
                const auto producer = static_cast<std::size_t>(value >> 40U);
                const auto seq = static_cast<std::int64_t>(value & ((1ULL << 40U) - 1));
                if (seq <= last[producer]) {
                    order_ok.store(false, std::memory_order_relaxed);
                }
                last[producer] = seq;
                const std::size_t index = producer * per_producer + static_cast<std::size_t>(seq);
                seen[index].fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    bool exactly_once = true;
    for (const auto& s : seen) {
        exactly_once = exactly_once && s.load() == 1;
    }
    CHECK(exactly_once);
    CHECK(order_ok.load());
    CHECK(queue.empty_approx());
}

}  // namespace

TEST_CASE("mpmc: stress 1P/1C") { run_stress(1, 1, 64); }
TEST_CASE("mpmc: stress 4P/1C") { run_stress(4, 1, 64); }
TEST_CASE("mpmc: stress 1P/4C") { run_stress(1, 4, 64); }
TEST_CASE("mpmc: stress 4P/4C tiny capacity") { run_stress(4, 4, 2); }

LFC_TEST_MAIN()
