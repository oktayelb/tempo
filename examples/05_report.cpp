// 05 — One summary for the whole run
//
// Printing a block per call is unreadable past a few dozen calls, so
// TEMPO_PRINT_ENABLED defaults to 0 and no per-call cout is in the build at all.
// Statistics are collected regardless, and every metric registers itself on its
// first call -- so tempo::report() prints everything that ran, sorted by total
// time, without you listing anything by hand.

#include "tempo.hpp"

#include <iostream>
#include <thread>
#include <vector>

int fast_path(int value) { return value * 2; }

// Unsigned: the running sum passes INT_MAX well before these loops end and
// signed overflow is undefined. Burning time is all they are for.
int slow_path(int rounds) {
    volatile unsigned int sink = 0;
    for (int i = 0; i < rounds * 20000; ++i) { sink = sink + static_cast<unsigned int>(i); }
    return static_cast<int>(sink);
}

int worker(int rounds) {
    volatile unsigned int sink = 0;
    for (int i = 0; i < rounds * 500; ++i) { sink = sink + static_cast<unsigned int>(i); }
    return static_cast<int>(sink);
}

int main() {
    TEMPO_CALLABLE_METRICS(fast_path) fast;
    TEMPO_CALLABLE_METRICS(slow_path) slow;

    for (int i = 0; i < 50; ++i) { fast(i); }
    for (int i = 1; i <= 5; ++i) { slow(i); }

    // The same metric from several threads: the statistics are behind a mutex,
    // and the lock is never held while the profiled function runs -- it is taken
    // only after the clock has stopped.
    TEMPO_CALLABLE_METRICS(worker) shared;
    std::vector<std::thread> pool;
    for (int t = 0; t < 4; ++t) {
        pool.emplace_back([&shared] {
            for (int i = 0; i < 500; ++i) { shared(1 + i % 8); }
        });
    }
    for (auto& thread : pool) { thread.join(); }

    // Rows are sorted by total time, so the hot spot is the first line.
    tempo::report();

    // tempo::report_at_exit() installs the same summary to print when the
    // program ends, and tempo::reset_all() clears every registered metric.
}
