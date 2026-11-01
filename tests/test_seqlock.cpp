// SPDX-License-Identifier: MIT
#include <array>
#include <atomic>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

#include "lfc/seqlock.hpp"
#include "test_framework.hpp"
#include "test_support.hpp"

using lfc::Seqlock;

namespace {

/// 40-byte payload whose fields are all derived from `id`, so a torn read
/// (fields from two different writes) is detectable.
struct Pose {
    std::uint64_t id;
    double x;
    double y;
    double z;
    std::uint64_t checksum;
};

Pose make_pose(std::uint64_t id) {
    const auto f = static_cast<double>(id);
    return Pose{id, f * 0.5, f * 2.0, -f, id ^ 0xA5A5'A5A5'A5A5'A5A5ULL};
}

bool consistent(const Pose& p) {
    const Pose expected = make_pose(p.id);
    return p.x == expected.x && p.y == expected.y && p.z == expected.z &&
           p.checksum == expected.checksum;
}

}  // namespace

TEST_CASE("seqlock: store and load single-threaded") {
    Seqlock<Pose> lock(make_pose(1));
    CHECK(lock.version() == 0);
    CHECK(lock.load().id == 1);
    lock.store(make_pose(42));
    CHECK(lock.version() == 1);
    const auto snapshot = lock.try_load();
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->id == 42);
    CHECK(consistent(*snapshot));
}

TEST_CASE("seqlock: odd-sized payload") {
    struct Odd {
        std::array<char, 13> bytes;
    };
    Seqlock<Odd> lock;
    Odd value{};
    std::iota(value.bytes.begin(), value.bytes.end(), 'a');
    lock.store(value);
    const Odd out = lock.load();
    CHECK(out.bytes == value.bytes);
}

TEST_CASE("seqlock: readers never observe torn or regressing values") {
    constexpr unsigned kReaders = 3;
    const std::uint64_t writes = lfc_test::scaled(500'000);
    Seqlock<Pose> lock(make_pose(0));
    std::atomic<bool> done{false};
    std::atomic<bool> torn{false};
    std::atomic<bool> regressed{false};
    std::vector<std::thread> readers;
    for (unsigned r = 0; r < kReaders; ++r) {
        readers.emplace_back([&] {
            std::uint64_t last = 0;
            while (!done.load(std::memory_order_acquire)) {
                const Pose p = lock.load();
                if (!consistent(p)) {
                    torn.store(true);
                }
                if (p.id < last) {
                    regressed.store(true);
                }
                last = p.id;
            }
        });
    }
    for (std::uint64_t i = 1; i <= writes; ++i) {
        lock.store(make_pose(i));
    }
    done.store(true, std::memory_order_release);
    for (auto& t : readers) {
        t.join();
    }
    CHECK(!torn.load());
    CHECK(!regressed.load());
    CHECK(lock.load().id == writes);
}

LFC_TEST_MAIN()
