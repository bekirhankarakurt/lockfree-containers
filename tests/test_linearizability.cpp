// SPDX-License-Identifier: MIT
// Records small concurrent histories against each container and verifies that
// every history is linearizable with respect to the sequential specification.
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include "lfc/lockfree_stack.hpp"
#include "lfc/mpmc_queue.hpp"
#include "lfc/spsc_ring.hpp"
#include "linearizability.hpp"
#include "test_framework.hpp"
#include "test_support.hpp"

using lfc_test::FifoModel;
using lfc_test::HistoryClock;
using lfc_test::LifoModel;
using lfc_test::LinearizabilityChecker;
using lfc_test::Op;
using lfc_test::OpKind;

namespace {

constexpr unsigned kThreads = 3;
constexpr int kOpsPerThread = 4;

/// Per-thread script: true = push, false = pop.
using Script = std::vector<bool>;

std::vector<Script> random_scripts(std::mt19937& rng, unsigned threads) {
    std::bernoulli_distribution coin(0.5);
    std::vector<Script> scripts(threads);
    for (auto& s : scripts) {
        for (int i = 0; i < kOpsPerThread; ++i) {
            s.push_back(coin(rng));
        }
    }
    return scripts;
}

/// Runs one thread's script; pushes use unique values (thread * 100 + i).
template <typename Container, typename Pop>
void run_script(Container& c, const Script& script, unsigned thread, HistoryClock& clock,
                lfc_test::StartGate& gate, std::vector<Op>& out, Pop&& pop) {
    gate.arrive_and_wait();
    int i = 0;
    for (const bool is_push : script) {
        Op op;
        op.invoke = clock.tick();
        if (is_push) {
            op.kind = OpKind::Push;
            op.value = static_cast<int>(thread) * 100 + i;
            op.ok = c.try_push(op.value);
        } else {
            op.kind = OpKind::Pop;
            op.ok = pop(c, op.value);
        }
        op.response = clock.tick();
        out.push_back(op);
        ++i;
    }
}

template <typename Model>
bool linearizable(const std::vector<std::vector<Op>>& per_thread, Model model) {
    std::vector<Op> history;
    for (const auto& ops : per_thread) {
        history.insert(history.end(), ops.begin(), ops.end());
    }
    return LinearizabilityChecker<Model>(std::move(history), std::move(model)).check();
}

}  // namespace

TEST_CASE("linearizability: MPMC queue (blocking pops)") {
    std::mt19937 rng(12345);
    const std::size_t rounds = lfc_test::scaled(3000);
    std::size_t failures = 0;
    for (std::size_t round = 0; round < rounds; ++round) {
        // Vyukov's try_pop may report "empty" while an earlier-claimed push is
        // in flight, which is not linearizable for *failed* pops, so pops here
        // retry until they succeed. Each thread never pops more than it has
        // pushed so far, so a waiting pop always has an element available.
        auto scripts = random_scripts(rng, kThreads);
        for (auto& s : scripts) {
            int balance = 0;
            for (std::size_t k = 0; k < s.size(); ++k) {
                if (!s[k] && balance <= 0) {
                    s[k] = true;
                }
                balance += s[k] ? 1 : -1;
            }
        }
        lfc::MpmcQueue<int> queue(64);
        HistoryClock clock;
        lfc_test::StartGate gate(kThreads);
        std::vector<std::vector<Op>> ops(kThreads);
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t] {
                run_script(queue, scripts[t], t, clock, gate, ops[t], [](auto& q, int& v) {
                    while (!q.try_pop(v)) {
                        std::this_thread::yield();
                    }
                    return true;
                });
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        failures += linearizable(ops, FifoModel(64)) ? 0U : 1U;
    }
    CHECK(failures == 0);
}

TEST_CASE("linearizability: lock-free stack (including empty pops)") {
    std::mt19937 rng(777);
    const std::size_t rounds = lfc_test::scaled(3000);
    std::size_t failures = 0;
    for (std::size_t round = 0; round < rounds; ++round) {
        const auto scripts = random_scripts(rng, kThreads);
        lfc::LockFreeStack<int> stack(64);
        HistoryClock clock;
        lfc_test::StartGate gate(kThreads);
        std::vector<std::vector<Op>> ops(kThreads);
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t] {
                run_script(stack, scripts[t], t, clock, gate, ops[t],
                           [](auto& s, int& v) { return s.try_pop(v); });
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        failures += linearizable(ops, LifoModel(64)) ? 0U : 1U;
    }
    CHECK(failures == 0);
}

TEST_CASE("linearizability: SPSC ring (full and empty results)") {
    const std::size_t rounds = lfc_test::scaled(3000);
    // Capacity 2 so "full" results are exercised; thread 0 only pushes,
    // thread 1 only pops (the SPSC contract).
    const std::vector<Script> scripts{Script(6, true), Script(6, false)};
    std::size_t failures = 0;
    for (std::size_t round = 0; round < rounds; ++round) {
        lfc::SpscRing<int> ring(2);
        HistoryClock clock;
        lfc_test::StartGate gate(2);
        std::vector<std::vector<Op>> ops(2);
        std::thread producer([&] {
            run_script(ring, scripts[0], 0, clock, gate, ops[0],
                       [](auto& r, int& v) { return r.try_pop(v); });
        });
        std::thread consumer([&] {
            run_script(ring, scripts[1], 1, clock, gate, ops[1],
                       [](auto& r, int& v) { return r.try_pop(v); });
        });
        producer.join();
        consumer.join();
        failures += linearizable(ops, FifoModel(2)) ? 0U : 1U;
    }
    CHECK(failures == 0);
}

TEST_CASE("linearizability: checker rejects a non-linearizable history") {
    // push(1) completes, then push(2) completes, then pop returns 2: not FIFO.
    const std::vector<Op> history{
        Op{OpKind::Push, 1, true, 0, 1},
        Op{OpKind::Push, 2, true, 2, 3},
        Op{OpKind::Pop, 2, true, 4, 5},
    };
    CHECK(!LinearizabilityChecker<FifoModel>(history, FifoModel(8)).check());
    CHECK(LinearizabilityChecker<LifoModel>(history, LifoModel(8)).check());
}

LFC_TEST_MAIN()
