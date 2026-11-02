# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-11-04

### Added

- `lfc::SpscRing` - wait-free single-producer/single-consumer ring buffer with
  cached indices and a zero-copy `front()`/`pop_front()` interface.
- `lfc::MpmcQueue` - bounded lock-free multi-producer/multi-consumer queue
  (Vyukov sequence-number design) with exponential backoff.
- `lfc::LockFreeStack` - bounded Treiber stack with tagged-index ABA protection
  and a pre-allocated node pool (no allocation after construction).
- `lfc::Seqlock` - single-writer sequence lock, data-race free per the C++
  memory model and clean under ThreadSanitizer.
- `lfc::TripleBuffer` - wait-free latest-value channel for sensor-style data.
- Dependency-free unit, stress and linearizability tests run through CTest.
- Benchmark harness comparing against `std::mutex` baselines (throughput,
  latency percentiles, latest-value sharing).
- CMake install rules and `find_package(lfc)` package config.
- CI: GCC and Clang, ASan/UBSan, TSan, clang-tidy, cppcheck, CodeQL, coverage
  and Doxygen API docs on GitHub Pages, tagged releases.

[Unreleased]: https://github.com/bekirhankarakurt/lockfree-containers/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/bekirhankarakurt/lockfree-containers/releases/tag/v1.0.0
