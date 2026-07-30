// 02 — Timing, and the input that was slowest
//
// A sampling profiler tells you that fibonacci() burned 90% of the runtime. It
// does not tell you which call did it. tempo wraps the call site and knows the
// signature, so it keeps the argument values of the fastest and the slowest
// call: the answer is "n = 32", not just "fibonacci is hot".

#include "tempo.hpp"

#include <iostream>

unsigned long long fibonacci(unsigned n) {
    return n < 2 ? n : fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    TEMPO_CALLABLE_METRICS(fibonacci) fib;

    // Deliberately out of order, so "slowest" cannot simply mean "last".
    for (const unsigned n : {26u, 22u, 32u, 20u, 30u}) {
        // An ordinary call. It also records the caller's file and line: the
        // wrapper's operator() carries the function's own parameter list plus a
        // trailing source_location that defaults at the call site.
        fib(n);
    }

    // Every statistic under one lock, so these numbers describe one moment.
    const auto stats = fib.snapshot();

    // min_args and max_args are tuples of the parameters as declared, so a
    // structured binding names them.
    const auto [fastest_n] = stats.min_args;
    const auto [slowest_n] = stats.max_args;

    std::cout << "calls   : " << stats.calls << "\n"
              << "total   : " << stats.total_duration.count() << " ms\n"
              << "average : " << stats.average_ms() << " ms\n"
              << "fastest : n = " << fastest_n
              << "  (" << stats.min_duration.count() << " ms)\n"
              << "slowest : n = " << slowest_n
              << "  (" << stats.max_duration.count() << " ms)\n"
              << "last call site: " << stats.last_call_location.file_name()
              << ":" << stats.last_call_location.line() << "\n";

    // Only the outermost call goes through the wrapper -- fibonacci recurses
    // into itself directly -- so this is whole-call latency, not per-level cost.
    // See 06_recursion.cpp for counting every level.
}
