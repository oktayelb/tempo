// 06 — Recursion, depth gating, and the counting switch
//
// This file is built BOTH ways by the Makefile's matrix target, and asserts the
// right thing in each mode. The invariant that must hold in both, and the reason
// depth gating exists at all: the recorded total tracks the wall clock instead
// of summing intervals that contain one another.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

TEMPO_RECURSIVE(int, fib, unsigned n) {
    return n < 2 ? static_cast<int>(n)
                 : TEMPO_SELF(fib)(n - 1) + TEMPO_SELF(fib)(n - 2);
}

TEMPO_RECURSIVE(unsigned long long, factorial, unsigned n) {
    return n < 2 ? 1ull : n * TEMPO_SELF(factorial)(n - 1);
}

TEMPO_RECURSIVE(int, countdown, int n) {
    return n <= 0 ? 0 : 1 + TEMPO_SELF(countdown)(n - 1);
}

// Throws at the bottom, so the stack unwinds through every level.
TEMPO_RECURSIVE(int, dive_and_throw, int n) {
    if (n <= 0) { throw std::runtime_error("bottom"); }
    return TEMPO_SELF(dive_and_throw)(n - 1);
}

// A plain, non-recursive instrumented function: nothing about it may change.
namespace impl {
int square(int x) { return x * x; }
}
TEMPO_INSTRUMENT(impl::square, square);

// Fibonacci call counts: fib(n) makes 2*F(n+1)-1 calls in total.
namespace {
[[maybe_unused]] unsigned expected_fib_calls(unsigned n) {
    unsigned long long a = 1, b = 1;   // F(1), F(2)
    for (unsigned i = 1; i < n + 1; ++i) { const auto next = a + b; a = b; b = next; }
    return static_cast<unsigned>(2ull * a - 1ull);
}
}  // namespace

TEST(result_is_correct_in_either_mode) {
    decltype(fib)::reset();
    CHECK_EQ(fib(20), 6765);
    CHECK_EQ(fib(0), 0);
    CHECK_EQ(fib(1), 1);
    CHECK_EQ(factorial(10), 3628800ull);
    CHECK_EQ(countdown(50), 50);
}

TEST(depth_gate_keeps_the_total_honest) {
    decltype(fib)::reset();

    const auto begin = std::chrono::steady_clock::now();
    fib(21);
    const auto end = std::chrono::steady_clock::now();
    const double wall = std::chrono::duration<double, std::milli>(end - begin).count();

    const auto stats = decltype(fib)::snapshot();

    // Exactly one call was timed, however many were counted.
    CHECK_EQ(stats.timed_calls, 1u);

    // The headline invariant. Timing every recursion level instead would report
    // roughly 15x the wall clock, because each level would re-count everything
    // beneath it. A generous factor here so a loaded CI machine cannot flake it.
    CHECK_LE(stats.total_duration.count(), wall * 2.0);
    CHECK_GT(stats.total_duration.count(), 0.0);

    // With a single timed call, min, max, average and total all coincide.
    CHECK_NEAR(stats.min_duration.count(), stats.max_duration.count(), 1e-9);
    CHECK_NEAR(stats.average_ms(), stats.total_duration.count(), 1e-9);
}

TEST(average_divides_by_timed_calls_not_by_every_level) {
    decltype(fib)::reset();
    fib(18);
    fib(18);

    const auto stats = decltype(fib)::snapshot();
    CHECK_EQ(stats.timed_calls, 2u);
    CHECK_NEAR(stats.average_ms(), stats.total_duration.count() / 2.0, 1e-9);

    // The average must sit inside the observed range. Dividing by the full call
    // count instead would push it far below the minimum -- that was a real bug.
    CHECK_GE(stats.average_ms(), stats.min_duration.count() * 0.99);
    CHECK_LE(stats.average_ms(), stats.max_duration.count() * 1.01);
}

TEST(counting_switch_behaves_as_documented) {
    decltype(fib)::reset();
    fib(20);
    const auto stats = decltype(fib)::snapshot();

#if TEMPO_COUNT_RECURSION
    CHECK_EQ(stats.calls, expected_fib_calls(20));
    CHECK_EQ(stats.max_depth, 20u);
#else
    // Recursion goes straight to the real function: invisible and free.
    CHECK_EQ(stats.calls, 1u);
    CHECK_EQ(stats.max_depth, 1u);
#endif
}

TEST(depth_matches_the_shape_of_the_recursion) {
    decltype(countdown)::reset();
    countdown(100);

    const auto stats = decltype(countdown)::snapshot();
#if TEMPO_COUNT_RECURSION
    CHECK_EQ(stats.calls, 101u);       // 100 down to 0, inclusive
    CHECK_EQ(stats.max_depth, 101u);   // linear recursion: depth equals calls
#else
    CHECK_EQ(stats.calls, 1u);
    CHECK_EQ(stats.max_depth, 1u);
#endif
}

TEST(max_depth_keeps_the_deepest_across_calls) {
    decltype(countdown)::reset();

    countdown(10);
    countdown(200);
    countdown(5);

    const auto stats = decltype(countdown)::snapshot();
    CHECK_EQ(stats.timed_calls, 3u);
#if TEMPO_COUNT_RECURSION
    // The deepest run wins, and a later shallow run does not lower it.
    CHECK_EQ(stats.max_depth, 201u);
#else
    CHECK_EQ(stats.max_depth, 1u);
#endif
}

TEST(depth_returns_to_zero_between_calls) {
    decltype(fib)::reset();
    CHECK_EQ(decltype(fib)::current_depth(), 0u);

    fib(15);
    CHECK_EQ(decltype(fib)::current_depth(), 0u);

    fib(15);
    CHECK_EQ(decltype(fib)::current_depth(), 0u);
}

TEST(depth_is_restored_when_a_call_throws) {
    decltype(dive_and_throw)::reset();

    for (int attempt = 0; attempt < 5; ++attempt) {
        CHECK_THROWS_AS(dive_and_throw(20), std::runtime_error);
        // If the decrement were skipped on the unwinding path, depth would
        // climb and every later call would look like an inner one -- silently
        // never timed again.
        CHECK_EQ(decltype(dive_and_throw)::current_depth(), 0u);
    }

    // A successful call afterwards must still be timed.
    decltype(countdown)::reset();
    countdown(3);
    CHECK_EQ(decltype(countdown)::snapshot().timed_calls, 1u);
    CHECK(decltype(countdown)::snapshot().has_samples);
}

TEST(a_throwing_recursion_records_no_duration) {
    decltype(dive_and_throw)::reset();

    CHECK_THROWS_AS(dive_and_throw(10), std::runtime_error);

    const auto stats = decltype(dive_and_throw)::snapshot();
    CHECK_EQ(stats.timed_calls, 0u);
    CHECK(!stats.has_samples);
    CHECK_EQ(stats.total_duration.count(), 0.0);
#if TEMPO_COUNT_RECURSION
    CHECK_EQ(stats.calls, 11u);   // counted on the way down, before the throw
#else
    CHECK_EQ(stats.calls, 1u);
#endif
}

TEST(threads_recursing_do_not_disturb_each_others_depth) {
    decltype(fib)::reset();

    constexpr int thread_count = 4;
    constexpr unsigned argument = 16;

    std::vector<std::thread> workers;
    for (int i = 0; i < thread_count; ++i) {
        workers.emplace_back([] { fib(argument); });
    }
    for (auto& worker : workers) { worker.join(); }

    const auto stats = decltype(fib)::snapshot();
    CHECK_EQ(stats.timed_calls, static_cast<unsigned>(thread_count));

#if TEMPO_COUNT_RECURSION
    CHECK_EQ(stats.calls, thread_count * expected_fib_calls(argument));
    // Depth is per thread, so four concurrent recursions must not stack into
    // 4x the depth of one.
    CHECK_EQ(stats.max_depth, argument);
#else
    CHECK_EQ(stats.calls, static_cast<unsigned>(thread_count));
    CHECK_EQ(stats.max_depth, 1u);
#endif

    CHECK_EQ(decltype(fib)::current_depth(), 0u);
}

TEST(non_recursive_functions_are_untouched_by_any_of_this) {
    decltype(square)::reset();

    square(3);
    square(4);

    const auto stats = decltype(square)::snapshot();
    CHECK_EQ(stats.calls, 2u);
    CHECK_EQ(stats.timed_calls, 2u);
    // Depth is always exactly 1 for an ordinary call, in either mode.
    CHECK_EQ(stats.max_depth, 1u);
    CHECK_NEAR(stats.average_ms(), stats.total_duration.count() / 2.0, 1e-9);
}

TEST(instrument_never_routes_recursion_through_the_wrapper) {
    // TEMPO_INSTRUMENT does not change a function body, so even if that body
    // recursed it would call itself directly. Counting stays at the base call
    // regardless of TEMPO_COUNT_RECURSION.
    decltype(square)::reset();
    square(5);
    CHECK_EQ(decltype(square)::snapshot().calls, 1u);
    CHECK_EQ(decltype(square)::snapshot().max_depth, 1u);
}

TEST(report_shows_the_depth_column_only_when_something_recursed) {
    tempo::reset_all();

    fib(12);
    square(2);

    std::ostringstream out;
    tempo::report(out);
    const std::string text = out.str();

#if TEMPO_COUNT_RECURSION
    CHECK(text.find("depth") != std::string::npos);
#else
    // Nothing recursed through a wrapper, so the table is the original one.
    CHECK(text.find("depth") == std::string::npos);
#endif
    CHECK(text.find("fib") != std::string::npos);
}

TEST(reset_clears_depth_statistics) {
    decltype(countdown)::reset();
    countdown(30);

    decltype(countdown)::reset();
    const auto stats = decltype(countdown)::snapshot();
    CHECK_EQ(stats.max_depth, 0u);
    CHECK_EQ(stats.calls, 0u);
    CHECK_EQ(stats.timed_calls, 0u);
    CHECK_EQ(decltype(countdown)::current_depth(), 0u);
}

TEST(deep_recursion_does_not_lose_count) {
    decltype(countdown)::reset();

    // Deep enough to be interesting, shallow enough to be safe on a small
    // default stack under a sanitizer.
    countdown(2000);

    const auto stats = decltype(countdown)::snapshot();
#if TEMPO_COUNT_RECURSION
    CHECK_EQ(stats.calls, 2001u);
    CHECK_EQ(stats.max_depth, 2001u);
#else
    CHECK_EQ(stats.calls, 1u);
#endif
    CHECK_EQ(stats.timed_calls, 1u);
    CHECK_EQ(decltype(countdown)::current_depth(), 0u);
}
