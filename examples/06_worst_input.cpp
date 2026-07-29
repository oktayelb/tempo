// 06 — Finding the input that made it slow
//
// A sampling profiler tells you that fibonacci() burned 90% of your runtime.
// It does not tell you which call did it. Because tempo wraps the call site and
// knows the signature, it can keep the argument values of the slowest call --
// so the answer is "n = 32", not just "fibonacci is hot".

#include "tempo.hpp"

#include <cassert>
#include <iostream>
#include <vector>

unsigned long long fibonacci(unsigned n) {
    return n < 2 ? n : fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    using FibMetrics = TEMPO_CALLABLE_METRICS(fibonacci);
    FibMetrics::reset();
    FibMetrics fib;

    // Deliberately out of order, so "slowest" cannot simply mean "last".
    const std::vector<unsigned> workload{26, 22, 32, 20, 30};

    std::cout << "running " << workload.size() << " calls...\n\n";
    for (const unsigned n : workload) {
        TEMPO_METRICS_CALL(fib, n);
    }

    const auto fastest = fib.get_minimizers();
    const auto slowest = fib.get_maximizers();

    std::cout << "\n=============== summary ===============\n";
    std::cout << "calls        : " << FibMetrics::call_count << "\n";
    std::cout << "total time   : " << FibMetrics::total_duration.count() << " ms\n";
    std::cout << "average      : "
              << FibMetrics::total_duration.count() / FibMetrics::call_count << " ms\n";
    std::cout << "fastest call : n = " << std::get<0>(fastest)
              << "  (" << FibMetrics::min_duration.count() << " ms)\n";
    std::cout << "slowest call : n = " << std::get<0>(slowest)
              << "  (" << FibMetrics::max_duration.count() << " ms)\n";

    // The workload's worst input is 32 and its best is 20, and tempo found both
    // without anyone writing a timer.
    assert(std::get<0>(slowest) == 32);
    assert(std::get<0>(fastest) == 20);
    std::cout << "\nThe pathological input was n = " << std::get<0>(slowest) << ".\n";

    // Two things worth understanding about this number:
    //
    // 1. Only the outer call goes through the wrapper. fibonacci() recurses
    //    into itself directly, so what you measured is whole-call latency, not
    //    per-recursion cost. That is usually what you want.
    //
    // 2. tempo prints its report inside the timed region, so every measurement
    //    carries a few microseconds of I/O overhead. It disappears against the
    //    milliseconds here; it would not against a function that returns in
    //    nanoseconds.
}
