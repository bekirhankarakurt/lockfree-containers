# Contributing

Thanks for your interest in improving lockfree-containers.

## Development setup

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DLFC_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before opening a pull request, also run both sanitizer builds:

```bash
cmake -S . -B build-tsan -DLFC_SANITIZER=thread && cmake --build build-tsan -j && ctest --test-dir build-tsan
cmake -S . -B build-asan -DLFC_SANITIZER=address && cmake --build build-asan -j && ctest --test-dir build-asan
```

## Guidelines

- C++17, header-only, no third-party dependencies.
- Zero warnings with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
- Every atomic operation carries a comment naming the operation it pairs with
  and why the chosen memory order is sufficient.
- New containers need: unit tests, a multi-threaded stress test, a
  linearizability test if the container has a sequential specification, and
  a benchmark row against a mutex baseline.
- `clang-tidy` (see `.clang-tidy`) and `cppcheck` must pass; CI enforces both.
- Keep commits small and focused, with imperative subject lines
  ("Add ...", "Fix ...").

## Reporting bugs

Use the issue templates. For concurrency bugs, include the thread layout,
element type, compiler, flags and, if possible, ThreadSanitizer output.
Security issues go through [SECURITY.md](SECURITY.md).
