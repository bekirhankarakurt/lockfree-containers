// SPDX-License-Identifier: MIT
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "lfc/triple_buffer.hpp"
#include "test_framework.hpp"
#include "test_support.hpp"

using lfc::TripleBuffer;

TEST_CASE("triple buffer: latest value wins") {
    TripleBuffer<int> buffer(-1);
    CHECK(!buffer.has_fresh());
    CHECK(!buffer.update());
    CHECK(buffer.read_buffer() == -1);
    buffer.write(1);
    buffer.write(2);
    buffer.write(3);
    CHECK(buffer.has_fresh());
    CHECK(buffer.update());
    CHECK(buffer.read_buffer() == 3);
    CHECK(!buffer.update());
    CHECK(buffer.read() == 3);
}

TEST_CASE("triple buffer: in-place writes with non-trivial type") {
    TripleBuffer<std::vector<std::string>> buffer;
    auto& back = buffer.write_buffer();
    back.assign(3, "sample");
    buffer.publish();
    const auto& front = buffer.read();
    REQUIRE(front.size() == 3);
    CHECK(front[2] == "sample");
}

TEST_CASE("triple buffer: concurrent writer/reader see consistent, monotonic frames") {
    struct Frame {
        std::uint64_t id = 0;
        std::vector<std::uint64_t> samples = std::vector<std::uint64_t>(64, 0);
    };
    const std::uint64_t frames = lfc_test::scaled(300'000);
    TripleBuffer<Frame> buffer;
    std::atomic<bool> done{false};
    bool consistent = true;
    bool monotonic = true;
    std::uint64_t updates = 0;

    std::thread reader([&] {
        std::uint64_t last = 0;
        while (!done.load(std::memory_order_acquire) || buffer.has_fresh()) {
            if (!buffer.update()) {
                continue;
            }
            ++updates;
            const Frame& f = buffer.read_buffer();
            for (const auto s : f.samples) {
                consistent = consistent && s == f.id;
            }
            monotonic = monotonic && f.id > last;
            last = f.id;
        }
        consistent = consistent && last == frames;
    });
    for (std::uint64_t i = 1; i <= frames; ++i) {
        Frame& f = buffer.write_buffer();
        f.id = i;
        for (auto& s : f.samples) {
            s = i;
        }
        buffer.publish();
    }
    done.store(true, std::memory_order_release);
    reader.join();
    CHECK(consistent);
    CHECK(monotonic);
    CHECK(updates > 0);
}

LFC_TEST_MAIN()
