// 08 — Aggregated reporting, quiet mode, and threads
//
// Printing a block per call is unreadable past a few dozen calls, which is why
// TEMPO_PRINT_ENABLED defaults to 0 and no per-call cout is in the build at all.
// Statistics are collected regardless, and you get one sorted summary from
// tempo::report() whenever you want it.
//
// Every Metrics instantiation registers itself on its first call, so the report
// covers everything that ran without you listing anything by hand.

#include "tempo.hpp"

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

int fast_path(int value) { return value * 2; }

// unsigned, because the running sum passes INT_MAX well before these loops end
// and signed overflow is undefined behaviour. Unsigned wraparound is defined,
// and burning time is all these loops are for.
int slow_path(int rounds) {
    volatile unsigned int sink = 0;
    for (int i = 0; i < rounds * 20000; ++i) { sink = sink + static_cast<unsigned int>(i); }
    return static_cast<int>(sink);
}

int shared_worker(int rounds) {
    volatile unsigned int sink = 0;
    for (int i = 0; i < rounds * 500; ++i) { sink = sink + static_cast<unsigned int>(i); }
    return static_cast<int>(sink);
}

int main() {
    // --- a couple of ordinary workloads --------------------------------------
    using FastMetrics = TEMPO_CALLABLE_METRICS(fast_path);
    using SlowMetrics = TEMPO_CALLABLE_METRICS(slow_path);

    FastMetrics fast;
    SlowMetrics slow;

    for (int i = 0; i < 50; ++i) { fast(i); }
    for (int i = 1; i <= 5; ++i) { slow(i); }

    // Nothing was printed above: TEMPO_PRINT_ENABLED is 0, so those cout calls
    // are not in the binary at all. The statistics are still there.
    const auto fast_stats = FastMetrics::snapshot();
    assert(fast_stats.calls == 50);
    assert(fast_stats.has_samples);
    std::cout << "collected " << fast_stats.calls
              << " fast_path samples with no per-call output\n";

    // --- the same metrics hammered from several threads ----------------------
    // Statistics are guarded by a mutex, so concurrent calls aggregate
    // correctly. The lock is never held while the profiled function runs -- it
    // is taken only after the clock has already stopped.
    using SharedMetrics = TEMPO_CALLABLE_METRICS(shared_worker);
    SharedMetrics shared;

    constexpr int thread_count = 4;
    constexpr int calls_each = 500;

    std::vector<std::thread> pool;
    pool.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        pool.emplace_back([&shared] {
            for (int i = 0; i < calls_each; ++i) { shared(1 + i % 8); }
        });
    }
    for (auto& worker : pool) { worker.join(); }

    const auto shared_stats = SharedMetrics::snapshot();
    assert(shared_stats.calls == thread_count * calls_each);
    assert(shared_stats.min_duration <= shared_stats.max_duration);
    assert(shared_stats.total_duration >= shared_stats.max_duration);
    std::cout << "recorded " << shared_stats.calls << " calls from "
              << thread_count << " threads with no lost updates\n";

    // --- one sorted summary for everything that ran --------------------------
    // Rows are ordered by total time, so the hot spot is the first line.
    tempo::report();

    // reset_all() clears every registered metric at once.
    tempo::reset_all();
    assert(FastMetrics::snapshot().calls == 0);
    assert(SharedMetrics::snapshot().calls == 0);
    std::cout << "\nreset_all() cleared every registered metric\n";

    // tempo::report_at_exit() would install the same summary to print
    // automatically when the program ends.
}
