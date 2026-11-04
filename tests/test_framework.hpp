// SPDX-License-Identifier: MIT
// Minimal, dependency-free unit test harness.
#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace lfc_test {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

/// Total failed checks so far (atomic so checks are safe from worker threads).
inline std::atomic<int>& failure_count() {
    static std::atomic<int> failures{0};
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) {
        registry().push_back(TestCase{name, std::move(body)});
    }
};

inline void report_failure(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "  FAILED %s:%d: %s\n", file, line, expr);
    ++failure_count();
}

/// Runs every registered test; returns a process exit code.
inline int run_all() {
    int failed_tests = 0;
    for (const auto& test : registry()) {
        const int before = failure_count().load();
        std::printf("[ RUN  ] %s\n", test.name);
        try {
            test.body();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "  FAILED: unexpected exception: %s\n", e.what());
            ++failure_count();
        }
        const bool ok = failure_count().load() == before;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", test.name);
        failed_tests += ok ? 0 : 1;
    }
    std::printf("%zu tests, %d failed\n", registry().size(), failed_tests);
    return failed_tests == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace lfc_test

#define LFC_TEST_CONCAT_INNER(a, b) a##b
#define LFC_TEST_CONCAT(a, b) LFC_TEST_CONCAT_INNER(a, b)

/// Defines and registers a test case.
#define TEST_CASE(name)                                                              \
    static void LFC_TEST_CONCAT(lfc_test_fn_, __LINE__)();                           \
    static const ::lfc_test::Registrar LFC_TEST_CONCAT(lfc_test_reg_, __LINE__){     \
        name, &LFC_TEST_CONCAT(lfc_test_fn_, __LINE__)};                             \
    static void LFC_TEST_CONCAT(lfc_test_fn_, __LINE__)()

/// Records a failure but keeps running the test.
#define CHECK(expr)                                                \
    do {                                                           \
        if (!(expr)) {                                             \
            ::lfc_test::report_failure(__FILE__, __LINE__, #expr); \
        }                                                          \
    } while (false)

/// Records a failure and returns from the current test.
#define REQUIRE(expr)                                              \
    do {                                                           \
        if (!(expr)) {                                             \
            ::lfc_test::report_failure(__FILE__, __LINE__, #expr); \
            return;                                                \
        }                                                          \
    } while (false)

/// Checks that @p stmt throws @p exc.
#define CHECK_THROWS_AS(stmt, exc)                                                     \
    do {                                                                               \
        bool lfc_thrown = false;                                                       \
        try {                                                                          \
            stmt;                                                                      \
        } catch (const exc&) {                                                         \
            lfc_thrown = true;                                                         \
        }                                                                              \
        if (!lfc_thrown) {                                                             \
            ::lfc_test::report_failure(__FILE__, __LINE__, #stmt " throws " #exc);     \
        }                                                                              \
    } while (false)

#define LFC_TEST_MAIN() \
    int main() { return ::lfc_test::run_all(); }
