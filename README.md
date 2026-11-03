# lockfree-containers

**Header-only C++17 lock-free containers for real-time systems: SPSC ring, MPMC queue, ABA-safe stack, seqlock and triple buffer, with every memory order explained and verified under ThreadSanitizer.**

[![CI](https://github.com/bekirhankarakurt/lockfree-containers/actions/workflows/ci.yml/badge.svg)](https://github.com/bekirhankarakurt/lockfree-containers/actions/workflows/ci.yml)
[![CodeQL](https://github.com/bekirhankarakurt/lockfree-containers/actions/workflows/codeql.yml/badge.svg)](https://github.com/bekirhankarakurt/lockfree-containers/actions/workflows/codeql.yml)
[![Coverage](https://img.shields.io/endpoint?url=https://bekirhankarakurt.github.io/lockfree-containers/coverage/badge.json)](https://bekirhankarakurt.github.io/lockfree-containers/coverage/)
[![Release](https://img.shields.io/github/v/release/bekirhankarakurt/lockfree-containers)](https://github.com/bekirhankarakurt/lockfree-containers/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg?logo=cplusplus)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-linux%20x86--64-lightgrey.svg)](#requirements)

📖 **[Documentation](https://bekirhankarakurt.github.io/lockfree-containers/)** — [API reference](https://bekirhankarakurt.github.io/lockfree-containers/api/) · [Coverage report](https://bekirhankarakurt.github.io/lockfree-containers/coverage/)

![Benchmark demo](docs/demo.gif)

## Why

- **Deterministic hand-off between threads.** Sensor, control, logging and telemetry threads exchange data without locks, priority inversion or heap allocation after start-up.
- **Five primitives, one design language.** Queues for streams, a stack for buffer pools, a seqlock and a triple buffer for "latest value" state — all `try_*`, `noexcept` where possible, `[[nodiscard]]` everywhere.
- **Correctness you can audit.** Each `acquire`/`release` carries a comment naming the operation it pairs with. Tests include stress runs under TSan/ASan/UBSan and a linearizability checker for small concurrent histories.
- **Measured, not claimed.** A self-contained benchmark compares every container against a `std::mutex` baseline (throughput, p50–p99.9 latency).
- **Drop-in.** Header-only, no dependencies, `find_package(lfc)` or `add_subdirectory`.

## Quick start

```bash
git clone https://github.com/bekirhankarakurt/lockfree-containers.git
cd lockfree-containers
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/bench/lfc_bench --quick
```

Use it from your project (no install needed):

```cmake
include(FetchContent)
FetchContent_Declare(lfc
    GIT_REPOSITORY https://github.com/bekirhankarakurt/lockfree-containers.git
    GIT_TAG v1.0.0)
FetchContent_MakeAvailable(lfc)
target_link_libraries(my_app PRIVATE lfc::lfc)
```

…or install once and use `find_package`:

```bash
cmake -S . -B build -DLFC_BUILD_TESTS=OFF -DLFC_BUILD_BENCHMARKS=OFF -DLFC_BUILD_EXAMPLES=OFF
cmake --install build --prefix "$HOME/.local"
```

```cmake
find_package(lfc 1.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE lfc::lfc)
```

```cpp
#include <lfc/lfc.hpp>

lfc::SpscRing<int> ring(1024);          // capacity rounded up to a power of two
if (!ring.try_push(42)) { /* full */ }
if (auto v = ring.try_pop()) { /* use *v */ }
```

## Requirements

| Requirement | Version |
|---|---|
| C++ standard | C++17 |
| CMake | ≥ 3.16 |
| Compilers tested in CI | GCC 13.3, Clang 18.1 (Ubuntu 24.04, x86-64) |
| Dependencies | none (tests and benchmarks are self-contained) |
| Atomics | 64-bit lock-free CAS (`LockFreeStack::is_always_lock_free()`) |

### Build options

| Option | Default | Effect |
|---|---|---|
| `LFC_BUILD_TESTS` | `ON` (top level) | Unit, stress and linearizability tests |
| `LFC_BUILD_BENCHMARKS` | `ON` (top level) | `lfc_bench` harness |
| `LFC_BUILD_EXAMPLES` | `ON` (top level) | `sensor_pipeline` example |
| `LFC_SANITIZER` | empty | `address` (ASan + UBSan) or `thread` (TSan) |
| `LFC_WARNINGS_AS_ERRORS` | `OFF` | Adds `-Werror` / `/WX` |
| `LFC_COVERAGE` | `OFF` | `--coverage` instrumentation for gcovr |
| `LFC_INSTALL` | `ON` (top level) | Install rules + `lfcConfig.cmake` |
| `LFC_CACHE_LINE_SIZE` (macro) | auto | Override padding, e.g. `-DLFC_CACHE_LINE_SIZE=128` |

## API reference

All containers live in namespace `lfc`, are non-copyable and non-movable (they own shared atomics), and are bounded: capacity is fixed at construction and nothing allocates afterwards.

| Container | Header | Threads | Progress | Main operations |
|---|---|---|---|---|
| `SpscRing<T>` | `lfc/spsc_ring.hpp` | 1 producer, 1 consumer | wait-free | `try_push`, `try_emplace`, `try_pop(T&)`, `try_pop() -> optional`, `front`, `pop_front`, `size_approx`, `capacity` |
| `MpmcQueue<T>` | `lfc/mpmc_queue.hpp` | any | lock-free | `try_push`, `try_emplace`, `try_pop(T&)`, `try_pop() -> optional`, `size_approx`, `capacity` |
| `LockFreeStack<T>` | `lfc/lockfree_stack.hpp` | any | lock-free | `try_push`, `try_emplace`, `try_pop(T&)`, `try_pop() -> optional`, `empty_approx`, `capacity` |
| `Seqlock<T>` | `lfc/seqlock.hpp` | 1 writer, N readers | writer wait-free, readers lock-free | `store`, `load`, `try_load -> optional`, `version` |
| `TripleBuffer<T>` | `lfc/triple_buffer.hpp` | 1 writer, 1 reader | wait-free | `write`, `write_buffer` + `publish`, `update`, `read`, `read_buffer`, `has_fresh` |

Type requirements: all `T` must be nothrow-destructible; `MpmcQueue` and `LockFreeStack` also need nothrow move (a throwing move after a slot is claimed would wedge the slot); `Seqlock` needs a trivially copyable `T`. A rejected `try_push(T&&)` never consumes its argument. Full docs: [API reference](https://bekirhankarakurt.github.io/lockfree-containers/api/).

### Recipes

**1. Sensor → control loop, always the freshest sample** (`TripleBuffer`)

```cpp
struct Attitude { double roll, pitch, yaw; std::uint64_t t_ns; };
lfc::TripleBuffer<Attitude> attitude;

void imu_thread()   { attitude.write(read_imu()); }        // 1 kHz, never blocks
void control_tick() {                                       // 100 Hz, never blocks
    const Attitude& latest = attitude.read();               // stale samples skipped
    apply_control(latest);
}
```

**2. Real-time logging off the hot path, drop instead of stall** (`SpscRing`)

```cpp
lfc::SpscRing<LogRecord> log_ring(4096);

void log_hot(const LogRecord& r) {                          // control thread
    if (!log_ring.try_push(r)) { ++dropped; }               // bounded, wait-free
}
void logger_drain() {                                       // low-priority thread
    while (const LogRecord* r = log_ring.front()) {         // zero-copy peek
        write_to_disk(*r);
        log_ring.pop_front();
    }
}
```

**3. Worker pool** (`MpmcQueue`)

```cpp
using Job = std::function<void()>;
lfc::MpmcQueue<Job> jobs(1024);

bool submit(Job j) { return jobs.try_push(std::move(j)); }  // any thread
void worker() {
    Job job;
    while (running.load(std::memory_order_relaxed)) {
        if (jobs.try_pop(job)) job(); else std::this_thread::yield();
    }
}
```

**4. Pre-allocated packet-buffer pool** (`LockFreeStack` of indices)

```cpp
std::array<std::array<std::byte, 1500>, 256> packet_buffers;
lfc::LockFreeStack<std::uint32_t> free_buffers(256);       // filled with 0..255 at start-up

std::byte* acquire_buffer(std::uint32_t& id) {
    return free_buffers.try_pop(id) ? packet_buffers[id].data() : nullptr;
}
void release_buffer(std::uint32_t id) { (void)free_buffers.try_push(id); }
```

**5. Health snapshot read by many monitors** (`Seqlock`)

```cpp
struct HealthStatus { std::uint32_t mode, faults; double battery_v; };
lfc::Seqlock<HealthStatus> health;

void fdir_tick()      { health.store({mode, faults, battery_v}); }  // single writer
void telemetry_tick() { const HealthStatus s = health.load(); /* consistent copy */ }
```

A complete multi-threaded version of these patterns is in [`examples/sensor_pipeline.cpp`](examples/sensor_pipeline.cpp).

## Architecture

```
 SpscRing<T>                                   MpmcQueue<T>  (Vyukov)
 ┌──────────── cache line ────────────┐        enqueue_pos ──CAS──┐   ┌── CAS── dequeue_pos
 │ producer.index | producer.cached   │                           ▼   ▼
 ├──────────── cache line ────────────┤        ┌───────┬───────┬───────┬───────┐
 │ consumer.index | consumer.cached   │        │seq|T  │seq|T  │seq|T  │seq|T  │  one cell per
 └────────────────────────────────────┘        └───────┴───────┴───────┴───────┘  cache line
 slots: [T][T][T][T][T][T][T][T]  (power of 2)  seq==pos: free · pos+1: full · pos+cap: next lap

 LockFreeStack<T>                              TripleBuffer<T>
 data head  {tag:32 | index:32} ──► n5 ► n2 ► ∅      writer          middle          reader
 free head  {tag:32 | index:32} ──► n0 ► n7 ► …    ┌────────┐   ┌───────────────┐  ┌────────┐
 nodes: fixed array, recycled, never freed          │ back   │◄─►│ idx | fresh   │◄─►│ front  │
                                                    └────────┘   └───────────────┘  └────────┘
 Seqlock<T>                                          publish(): exchange      update(): exchange
 [ seq (odd = writing) ][ atomic<u64> words… ]
```

### Design trade-offs

| Decision | Benefit | Cost |
|---|---|---|
| Bounded, pre-allocated storage | No allocation after construction, predictable memory | Capacity must be sized up front |
| Power-of-two capacity | Index wrap is a mask, not a division | Up to 2× memory for odd sizes |
| SPSC cached opposite index | Shared line touched only when (apparently) full/empty | One extra word per side |
| One MPMC cell per cache line | No false sharing between neighbouring cells | Memory overhead for small `T` |
| Tagged *index* (32+32 bit) instead of tagged pointer | Single-width CAS, always lock-free, no `libatomic`/`cmpxchg16b` | Max 2³²−2 nodes; ABA possible only after 2³² CAS while one thread is stalled |
| Node pool instead of hazard pointers/epochs | Reclamation is trivially safe | Stack is bounded |
| Exponential backoff after failed CAS | ~2× (MPMC) to ~7× (stack) higher throughput under contention in `lfc_bench` | Slightly higher latency for the loser of a race |
| Seqlock payload as `atomic<uint64_t>` words | No data race in the C++ model; TSan-clean | `T` must be trivially copyable |
| `try_*` only, no blocking waits | Caller chooses spin, yield, sleep or drop | No built-in futex/condition-variable waiting |

## Memory ordering

Every atomic access uses the weakest order that is still correct; the "pairs with" column is how to audit it.

| Container | Operation | Order | Pairs with | Why |
|---|---|---|---|---|
| SpscRing | producer stores `tail+1` | release | consumer's acquire load of `tail` | element construction happens-before the consumer reads it |
| SpscRing | consumer stores `head+1` | release | producer's acquire load of `head` | consumer's read + destruction happen-before the slot is reused |
| SpscRing | own index load | relaxed | — | only the owning thread writes it |
| MpmcQueue | CAS on `enqueue_pos`/`dequeue_pos` | relaxed | — | only arbitrates who owns `pos`; data visibility rides on `seq` |
| MpmcQueue | producer stores `seq = pos+1` | release | consumer's acquire load of `seq` | publishes the element |
| MpmcQueue | consumer stores `seq = pos+cap` | release | next-lap producer's acquire load | old element destroyed before new construction |
| LockFreeStack | push CAS on head | release | pop's acquire CAS/load | publishes the node's `next` link and element |
| LockFreeStack | pop CAS on head | acquire (success and failure) | push's release CAS | sees `next` and the element; RMWs extend the release sequence |
| LockFreeStack | `node.next` load/store | relaxed | — | a stale value is discarded because the tag changed |
| Seqlock | writer `seq` odd store | relaxed | — | the following release payload stores cannot move above it |
| Seqlock | writer payload stores | release | reader's acquire payload loads | a reader that sees any new word must also see the odd `seq` |
| Seqlock | writer `seq` even store | release | reader's first acquire `seq` load | payload visible when an even, unchanged `seq` is read |
| Seqlock | reader `seq` re-check | relaxed | — | ordered after the acquire payload loads |
| TripleBuffer | `publish()` exchange | acq_rel | `update()` exchange | release publishes the back buffer; acquire ensures the reader finished with the buffer handed back |
| TripleBuffer | "fresh" peek | relaxed | — | hint only; the exchange provides ordering |

Note on Vyukov's queue: a failed `try_pop` means "the cell at the head is not ready", which can happen while a *later* producer has already finished. Successful operations are linearizable (verified by the test suite); treat a failed `try_pop`/`try_push` as "retry later", not as a proof of emptiness/fullness.

## Benchmarks

`./build/bench/lfc_bench` (Release, best of 3, 4 M ops per run). Machine: Intel Xeon @ 2.10 GHz, 4 vCPUs (cloud VM), Ubuntu 24.04, GCC 13.3, `-O3`.

**Queue throughput** (M items/s, higher is better)

| Producers × Consumers | lfc::SpscRing | lfc::MpmcQueue | mutex + std::queue | Best speed-up |
|---|---:|---:|---:|---:|
| 1 × 1 | 175.5 | 20.0 | 3.6 | 48.5× |
| 2 × 2 | – | 16.2 | 5.3 | 3.1× |
| 4 × 4 | – | 11.4 | 7.8 | 1.5× |
| 4 × 1 | – | 14.2 | 3.2 | 4.5× |
| 1 × 4 | – | 11.9 | 3.0 | 4.0× |

**Stack throughput** (M push+pop pairs/s)

| Threads | lfc::LockFreeStack | mutex + std::vector | Speed-up |
|---|---:|---:|---:|
| 1 | 24.6 | 27.4 | 0.9× |
| 2 | 20.8 | 5.7 | 3.7× |
| 4 | 16.6 | 7.6 | 2.2× |

**Round-trip latency, 1 × 1 ping-pong** (ns, lower is better)

| Queue | p50 | p90 | p99 | p99.9 |
|---|---:|---:|---:|---:|
| lfc::SpscRing | 450 | 536 | 601 | 5 591 |
| lfc::MpmcQueue | 334 | 400 | 488 | 9 540 |
| mutex + std::queue | 1 625 | 2 368 | 7 307 | 32 345 |

**Latest-value sharing**, 1 writer + 1 reader, 64-byte sample, writer publishing in a tight loop (M ops/s)

| Primitive | Reads/s | Writes/s |
|---|---:|---:|
| lfc::Seqlock | 6.0 | 15.6 |
| lfc::TripleBuffer | 180.6 | 21.9 |
| mutex + struct | 4.9 | 5.0 |

Reading the numbers: uncontended single-thread stack ops are on par with an uncontended mutex — the lock-free win appears with contention. In ping-pong, `MpmcQueue` edges out `SpscRing` because its sequence number shares a cache line with the payload, so each hop moves one line instead of two. A seqlock reader retries while the writer is mid-write; with a writer spinning flat out (worst case shown) reads drop, with a real 1 kHz sensor they don't. Shared VMs show large max-latency outliers (preemption), so maxima are omitted here; run `lfc_bench` on your target with pinned, isolated cores for representative tails.

## Testing

```bash
ctest --test-dir build --output-on-failure            # all tests (~3 s)
cmake -S . -B build-tsan -DLFC_SANITIZER=thread && cmake --build build-tsan -j && ctest --test-dir build-tsan
cmake -S . -B build-asan -DLFC_SANITIZER=address && cmake --build build-asan -j && ctest --test-dir build-asan
```

| Suite | What it checks |
|---|---|
| `spsc_ring`, `mpmc_queue`, `lockfree_stack`, `seqlock`, `triple_buffer` | Capacity rules, FIFO/LIFO order, full/empty, wrap-around, move-only types, exception safety, element lifetime (live-instance counting), multi-threaded stress with exactly-once delivery and per-producer ordering |
| `linearizability` | Thousands of random 3-thread histories recorded with a logical clock and checked against a sequential model (Wing & Gong search with memoization), plus a negative test proving the checker rejects bad histories |
| `benchmark_smoke`, `example_sensor_pipeline` | The benchmark and example run end-to-end |

CI runs GCC and Clang (Debug + Release, `-Werror`), ASan+UBSan and TSan (each test repeated 3×), clang-tidy, cppcheck, CodeQL, an install + `find_package` consumer build, and publishes [coverage](https://bekirhankarakurt.github.io/lockfree-containers/coverage/).

## Troubleshooting / FAQ

**TSan aborts with `unexpected memory mapping` / `FATAL: ThreadSanitizer`.**
Recent kernels use more ASLR entropy than the TSan runtime shipped with GCC 13 / Clang 18 supports. Run `sudo sysctl vm.mmap_rnd_bits=28` (CI does this).

**`MpmcQueue::try_pop` returned false but `size_approx()` > 0.**
Expected: the head cell's producer has claimed but not yet published its slot. Retry. See [Memory ordering](#memory-ordering).

**Why is my capacity different from what I asked for?**
`SpscRing` and `MpmcQueue` round up to a power of two (`MpmcQueue` minimum 2). `LockFreeStack` uses the exact value. Check `capacity()`.

**Compile error: "T must be nothrow move constructible and assignable".**
`MpmcQueue`/`LockFreeStack` require it; mark your move operations `noexcept` or store `std::unique_ptr<T>`.

**Should I override the cache-line size?**
On Apple M-series (128-byte lines) or to defeat Intel's adjacent-line prefetcher, compile with `-DLFC_CACHE_LINE_SIZE=128`. Use the same value in every translation unit.

**Is it safe in a hard real-time thread?**
`SpscRing`, `TripleBuffer` and `Seqlock::store` are wait-free and allocation-free after construction. `MpmcQueue` and `LockFreeStack` are lock-free (system-wide progress, but an individual thread can retry under contention).

## Project layout

```
lockfree-containers/
├── include/lfc/            # the library (header-only)
│   ├── config.hpp          #   cache-line size, raw storage, backoff
│   ├── spsc_ring.hpp       #   wait-free SPSC ring
│   ├── mpmc_queue.hpp      #   Vyukov bounded MPMC queue
│   ├── lockfree_stack.hpp  #   tagged-index Treiber stack + node pool
│   ├── seqlock.hpp         #   single-writer seqlock
│   ├── triple_buffer.hpp   #   wait-free latest-value buffer
│   ├── version.hpp         #   version constants
│   └── lfc.hpp             #   umbrella header
├── tests/                  # dependency-free test harness, stress + linearizability tests
│   └── package_consumer/   #   find_package smoke project used by CI
├── bench/                  # benchmark harness and mutex baselines
├── examples/               # sensor_pipeline.cpp: all five containers together
├── cmake/                  # build options, package config template
├── docs/                   # Doxyfile, Pages landing page, demo.tape/demo.gif
├── scripts/                # coverage badge generator
└── .github/                # CI, CodeQL, Pages, release workflows, templates
```

## Roadmap

- [x] SPSC ring, MPMC queue, ABA-safe stack, seqlock, triple buffer
- [x] Stress, sanitizer and linearizability testing
- [x] Benchmark harness with mutex baselines
- [x] CMake package config, tagged releases, API docs and coverage on Pages
- [ ] Optional blocking wait strategies (C++20 `atomic::wait` / futex)
- [ ] Batch `try_push_n` / `try_pop_n` for the SPSC ring
- [ ] AArch64 and MSVC jobs in CI
- [ ] Unbounded MPMC queue with hazard-pointer reclamation

## License

[MIT](LICENSE) © Bekirhan Karakurt

Bekirhan Karakurt - [github.com/bekirhankarakurt](https://github.com/bekirhankarakurt)
