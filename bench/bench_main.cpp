// SPDX-License-Identifier: MIT
// Self-contained benchmark harness: lock-free containers vs. mutex baselines.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "lfc/lfc.hpp"
#include "mutex_baselines.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::uint64_t ops = 4'000'000;
    std::uint64_t latency_samples = 200'000;
    std::chrono::milliseconds register_duration{300};
    int repeats = 3;
};

/// Spin barrier released by the coordinating thread.
class Gate {
public:
    void wait() const noexcept {
        while (!open_.load(std::memory_order_acquire)) {
        }
    }
    void open() noexcept { open_.store(true, std::memory_order_release); }

private:
    std::atomic<bool> open_{false};
};

/// Prevents the optimizer from discarding a computed value.
template <typename T>
void do_not_optimize(const T& value) {
#if defined(__GNUC__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    static volatile const T* sink = nullptr;
    sink = &value;
#endif
}

[[nodiscard]] double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// ---------------------------------------------------------------- throughput

/// Producers push `ops` items in total; consumers pop all of them.
/// Returns millions of items transferred per second.
template <typename Queue>
[[nodiscard]] double queue_throughput(unsigned producers, unsigned consumers, std::uint64_t ops) {
    Queue queue(1024);
    Gate gate;
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<unsigned> ready{0};
    const std::uint64_t per_producer = ops / producers;
    const std::uint64_t total = per_producer * producers;
    std::vector<std::thread> threads;
    for (unsigned p = 0; p < producers; ++p) {
        threads.emplace_back([&] {
            ready.fetch_add(1);
            gate.wait();
            for (std::uint64_t i = 0; i < per_producer; ++i) {
                while (!queue.try_push(i)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (unsigned c = 0; c < consumers; ++c) {
        threads.emplace_back([&] {
            ready.fetch_add(1);
            gate.wait();
            std::uint64_t value = 0;
            std::uint64_t local = 0;
            while (consumed.load(std::memory_order_relaxed) < total) {
                if (queue.try_pop(value)) {
                    do_not_optimize(value);
                    // Batch the shared counter update to keep it off the hot path.
                    if (++local == 64) {
                        consumed.fetch_add(local, std::memory_order_relaxed);
                        local = 0;
                    }
                } else {
                    if (local != 0) {
                        consumed.fetch_add(local, std::memory_order_relaxed);
                        local = 0;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }
    while (ready.load() != producers + consumers) {
        std::this_thread::yield();
    }
    const auto start = Clock::now();
    gate.open();
    for (auto& t : threads) {
        t.join();
    }
    return static_cast<double>(total) / seconds_since(start) / 1e6;
}

/// Each thread performs push+pop pairs. Returns millions of pairs per second.
template <typename Stack>
[[nodiscard]] double stack_throughput(unsigned thread_count, std::uint64_t ops) {
    Stack stack(1024);
    Gate gate;
    std::atomic<unsigned> ready{0};
    const std::uint64_t per_thread = ops / thread_count;
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < thread_count; ++t) {
        threads.emplace_back([&] {
            ready.fetch_add(1);
            gate.wait();
            std::uint64_t value = 0;
            for (std::uint64_t i = 0; i < per_thread; ++i) {
                while (!stack.try_push(i)) {
                }
                while (!stack.try_pop(value)) {
                }
                do_not_optimize(value);
            }
        });
    }
    while (ready.load() != thread_count) {
        std::this_thread::yield();
    }
    const auto start = Clock::now();
    gate.open();
    for (auto& t : threads) {
        t.join();
    }
    return static_cast<double>(per_thread * thread_count) / seconds_since(start) / 1e6;
}

template <typename Fn>
[[nodiscard]] double best_of(int repeats, const Fn& fn) {
    double best = 0.0;
    for (int i = 0; i < repeats; ++i) {
        best = std::max(best, fn());
    }
    return best;
}

// ------------------------------------------------------------------- latency

struct Percentiles {
    double p50 = 0;
    double p90 = 0;
    double p99 = 0;
    double p999 = 0;
    double max = 0;
};

[[nodiscard]] Percentiles percentiles(std::vector<std::uint64_t> samples) {
    std::sort(samples.begin(), samples.end());
    const auto at = [&](double q) {
        const auto index = static_cast<std::size_t>(q * static_cast<double>(samples.size() - 1));
        return static_cast<double>(samples[index]);
    };
    return Percentiles{at(0.50), at(0.90), at(0.99), at(0.999), static_cast<double>(samples.back())};
}

/// Ping-pong round trip through two queues (one per direction).
template <typename Queue>
[[nodiscard]] Percentiles round_trip_latency(std::uint64_t samples) {
    constexpr std::uint64_t kWarmup = 10'000;
    Queue ping(64);
    Queue pong(64);
    const std::uint64_t total = samples + kWarmup;
    std::thread echo([&] {
        std::uint64_t value = 0;
        for (std::uint64_t i = 0; i < total; ++i) {
            while (!ping.try_pop(value)) {
            }
            while (!pong.try_push(value)) {
            }
        }
    });
    std::vector<std::uint64_t> rtt;
    rtt.reserve(samples);
    std::uint64_t reply = 0;
    for (std::uint64_t i = 0; i < total; ++i) {
        const auto start = Clock::now();
        while (!ping.try_push(i)) {
        }
        while (!pong.try_pop(reply)) {
        }
        const auto elapsed = Clock::now() - start;
        if (i >= kWarmup) {
            rtt.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
        }
    }
    echo.join();
    return percentiles(std::move(rtt));
}

// ------------------------------------------------------ latest-value sharing

struct Sample {
    std::array<double, 7> channels{};
    std::uint64_t sequence = 0;
};

struct RegisterResult {
    double reads_per_sec_m = 0;
    double writes_per_sec_m = 0;
};

/// One writer publishes continuously while one reader polls for `duration`.
template <typename Write, typename Read>
[[nodiscard]] RegisterResult register_rates(std::chrono::milliseconds duration, const Write& write,
                                            const Read& read) {
    std::atomic<bool> stop{false};
    std::uint64_t writes = 0;
    std::thread writer([&] {
        Sample s;
        while (!stop.load(std::memory_order_relaxed)) {
            ++s.sequence;
            s.channels[0] = static_cast<double>(s.sequence);
            write(s);
            ++writes;
        }
    });
    std::uint64_t reads = 0;
    const auto start = Clock::now();
    while (Clock::now() - start < duration) {
        for (int i = 0; i < 256; ++i) {
            do_not_optimize(read().sequence);
        }
        reads += 256;
    }
    stop.store(true);
    writer.join();
    const double secs = seconds_since(start);
    return RegisterResult{static_cast<double>(reads) / secs / 1e6,
                          static_cast<double>(writes) / secs / 1e6};
}

// ------------------------------------------------------------------- output

[[nodiscard]] std::string cpu_model() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                return line.substr(colon + 2);
            }
        }
    }
    return "unknown";
}

void print_header(const Options& opt) {
    std::printf("# lockfree-containers benchmark (lfc %s)\n\n", LFC_VERSION_STRING);
    std::printf("CPU      : %s (%u hardware threads)\n", cpu_model().c_str(),
                std::thread::hardware_concurrency());
#if defined(__clang__)
    std::printf("Compiler : clang %s\n", __clang_version__);
#elif defined(__GNUC__)
    std::printf("Compiler : gcc %s\n", __VERSION__);
#endif
    std::printf("Settings : %llu ops/run, best of %d, %llu latency samples\n\n",
                static_cast<unsigned long long>(opt.ops), opt.repeats,
                static_cast<unsigned long long>(opt.latency_samples));
}

void run_queue_section(const Options& opt) {
    using Spsc = lfc::SpscRing<std::uint64_t>;
    using Mpmc = lfc::MpmcQueue<std::uint64_t>;
    using Mutex = lfc_bench::MutexQueue<std::uint64_t>;

    std::printf("## Queue throughput (M items/s, higher is better)\n\n");
    std::printf("| Producers x Consumers | lfc::SpscRing | lfc::MpmcQueue | mutex + std::queue | Best speed-up |\n");
    std::printf("|---|---:|---:|---:|---:|\n");
    const std::array<std::pair<unsigned, unsigned>, 5> scenarios{
        {{1, 1}, {2, 2}, {4, 4}, {4, 1}, {1, 4}}};
    for (const auto& scenario : scenarios) {
        const unsigned p = scenario.first;
        const unsigned c = scenario.second;
        const double mpmc = best_of(opt.repeats, [&] { return queue_throughput<Mpmc>(p, c, opt.ops); });
        const double mutex = best_of(opt.repeats, [&] { return queue_throughput<Mutex>(p, c, opt.ops); });
        double best = mpmc;
        std::string spsc_cell = "-";
        if (p == 1 && c == 1) {
            const double spsc = best_of(opt.repeats, [&] { return queue_throughput<Spsc>(1, 1, opt.ops); });
            std::array<char, 32> text{};
            std::snprintf(text.data(), text.size(), "%.1f", spsc);
            spsc_cell = text.data();
            best = std::max(best, spsc);
        }
        std::printf("| %u x %u | %s | %.1f | %.1f | %.1fx |\n", p, c, spsc_cell.c_str(), mpmc, mutex,
                    best / mutex);
        std::fflush(stdout);
    }
    std::printf("\n");
}

void run_stack_section(const Options& opt) {
    using Stack = lfc::LockFreeStack<std::uint64_t>;
    using Mutex = lfc_bench::MutexStack<std::uint64_t>;

    std::printf("## Stack throughput (M push+pop pairs/s, higher is better)\n\n");
    std::printf("| Threads | lfc::LockFreeStack | mutex + std::vector | Speed-up |\n");
    std::printf("|---|---:|---:|---:|\n");
    for (const unsigned threads : {1U, 2U, 4U}) {
        const double lf = best_of(opt.repeats, [&] { return stack_throughput<Stack>(threads, opt.ops / 2); });
        const double mx = best_of(opt.repeats, [&] { return stack_throughput<Mutex>(threads, opt.ops / 2); });
        std::printf("| %u | %.1f | %.1f | %.1fx |\n", threads, lf, mx, lf / mx);
        std::fflush(stdout);
    }
    std::printf("\n");
}

void run_latency_section(const Options& opt) {
    std::printf("## Round-trip latency, 1 x 1 ping-pong (ns, lower is better)\n\n");
    std::printf("| Queue | p50 | p90 | p99 | p99.9 | max |\n");
    std::printf("|---|---:|---:|---:|---:|---:|\n");
    const auto row = [](const char* name, const Percentiles& p) {
        std::printf("| %s | %.0f | %.0f | %.0f | %.0f | %.0f |\n", name, p.p50, p.p90, p.p99, p.p999, p.max);
        std::fflush(stdout);
    };
    row("lfc::SpscRing", round_trip_latency<lfc::SpscRing<std::uint64_t>>(opt.latency_samples));
    row("lfc::MpmcQueue", round_trip_latency<lfc::MpmcQueue<std::uint64_t>>(opt.latency_samples));
    row("mutex + std::queue",
        round_trip_latency<lfc_bench::MutexQueue<std::uint64_t>>(opt.latency_samples));
    std::printf("\n");
}

void run_register_section(const Options& opt) {
    std::printf("## Latest-value sharing, 1 writer + 1 reader, 64-byte sample (M ops/s)\n\n");
    std::printf("| Primitive | Reads/s | Writes/s |\n");
    std::printf("|---|---:|---:|\n");
    const auto row = [](const char* name, const RegisterResult& r) {
        std::printf("| %s | %.1f | %.1f |\n", name, r.reads_per_sec_m, r.writes_per_sec_m);
        std::fflush(stdout);
    };
    {
        lfc::Seqlock<Sample> seqlock;
        row("lfc::Seqlock", register_rates(
                                opt.register_duration, [&](const Sample& s) { seqlock.store(s); },
                                [&] { return seqlock.load(); }));
    }
    {
        lfc::TripleBuffer<Sample> triple;
        row("lfc::TripleBuffer", register_rates(
                                     opt.register_duration, [&](const Sample& s) { triple.write(s); },
                                     [&]() -> const Sample& { return triple.read(); }));
    }
    {
        lfc_bench::MutexRegister<Sample> mutex;
        row("mutex + struct", register_rates(
                                  opt.register_duration, [&](const Sample& s) { mutex.store(s); },
                                  [&] { return mutex.load(); }));
    }
    std::printf("\n");
}

void print_usage() {
    std::printf(
        "usage: lfc_bench [--quick] [--ops N] [--repeats N] [--section NAME]\n"
        "  --quick        small run for smoke tests / CI (~1-2 s)\n"
        "  --ops N        items per throughput run (default 4000000)\n"
        "  --repeats N    runs per data point, best is reported (default 3)\n"
        "  --section NAME one of: queue, stack, latency, register (default: all)\n");
}

[[nodiscard]] bool parse_u64(const char* text, std::uint64_t& out) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' || value == 0) {
        return false;
    }
    out = value;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    std::string section = "all";
    const std::vector<std::string> args(argv + 1, argv + argc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const bool has_value = i + 1 < args.size();
        std::uint64_t value = 0;
        if (arg == "--quick") {
            opt.ops = 200'000;
            opt.latency_samples = 20'000;
            opt.register_duration = std::chrono::milliseconds{50};
            opt.repeats = 1;
        } else if (arg == "--ops" && has_value && parse_u64(args[i + 1].c_str(), value)) {
            opt.ops = value;
            ++i;
        } else if (arg == "--repeats" && has_value && parse_u64(args[i + 1].c_str(), value)) {
            opt.repeats = static_cast<int>(std::min<std::uint64_t>(value, 100));
            ++i;
        } else if (arg == "--section" && has_value) {
            section = args[i + 1];
            ++i;
        } else {
            print_usage();
            return arg == "--help" || arg == "-h" ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
    const bool all = section == "all";
    if (!all && section != "queue" && section != "stack" && section != "latency" && section != "register") {
        print_usage();
        return EXIT_FAILURE;
    }

    print_header(opt);
    if (all || section == "queue") {
        run_queue_section(opt);
    }
    if (all || section == "stack") {
        run_stack_section(opt);
    }
    if (all || section == "latency") {
        run_latency_section(opt);
    }
    if (all || section == "register") {
        run_register_section(opt);
    }
    return EXIT_SUCCESS;
}
