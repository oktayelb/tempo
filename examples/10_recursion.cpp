// 10 — Recursion
//
// A recursive call is resolved at compile time to the function itself, so no
// library can intercept it: the body has to name the wrapper. TEMPO_RECURSIVE
// sets that up in one line, and TEMPO_SELF inside the body is the switch.
//
//   TEMPO_COUNT_RECURSION=0 (default)  every recursive call goes straight to the
//                                      real function: no wrapper, no cost, only
//                                      the outermost call counted.
//   TEMPO_COUNT_RECURSION=1            recursive calls go through the wrapper
//                                      and every one of them is counted.
//
// Build both ways to see the difference:
//     g++ -std=c++20 -O2 -I.. 10_recursion.cpp -o r0
//     g++ -std=c++20 -O2 -I.. -DTEMPO_COUNT_RECURSION=1 10_recursion.cpp -o r1

#include "tempo.hpp"

#include <cassert>
#include <chrono>
#include <iostream>

TEMPO_RECURSIVE(int, fibonacci, unsigned n) {
    return n < 2 ? static_cast<int>(n)
                 : TEMPO_SELF(fibonacci)(n - 1) + TEMPO_SELF(fibonacci)(n - 2);
}

// An ordinary function, for contrast: depth never exceeds 1 and nothing about
// its numbers changes whether recursion counting is on or off.
namespace impl {
int square(int x) { return x * x; }
}
TEMPO_INSTRUMENT(impl::square, square);

int main() {
    const auto begin = std::chrono::steady_clock::now();
    const int result = fibonacci(24);
    const auto end = std::chrono::steady_clock::now();
    const double wall =
        std::chrono::duration<double, std::milli>(end - begin).count();

    assert(result == 46368);

#if !TEMPO_ENABLED
    // Built with tempo switched off entirely: fibonacci is a plain function
    // pointer, so there is no wrapper to ask for statistics.
    std::cout << "TEMPO_ENABLED=0: fibonacci(24) = " << result << " in " << wall
              << " ms, with tempo entirely out of the build.\n";
    square(7);
#else
    const auto stats = decltype(fibonacci)::snapshot();

    std::cout << "TEMPO_COUNT_RECURSION = " << TEMPO_COUNT_RECURSION << "\n";
    std::cout << "  fibonacci(24)   : " << result << "\n";
    std::cout << "  calls counted   : " << stats.calls << "\n";
    std::cout << "  outermost calls : " << stats.timed_calls << "\n";
    std::cout << "  deepest recursion: " << stats.max_depth << "\n";
    std::cout << "  wall clock      : " << wall << " ms\n";
    std::cout << "  tempo total     : " << stats.total_duration.count() << " ms\n";
    std::cout << "  average per call: " << stats.average_ms() << " ms\n";

    // Whichever mode this was built in, only the outermost call is timed, so the
    // total tracks the wall clock instead of summing nested intervals. Timing a
    // recursive call inside its own parent would count the same work once per
    // level: for fibonacci(22) that is about 69 ms of "work" for 4.7 ms of time.
    assert(stats.timed_calls == 1);
    assert(stats.total_duration.count() <= wall * 1.5);

#if TEMPO_COUNT_RECURSION
    assert(stats.calls == 150049);   // every level
    assert(stats.max_depth == 24);
    std::cout << "  -> every recursive call counted, and the total still "
                 "matches the clock.\n";
#else
    assert(stats.calls == 1);        // the outermost call only
    assert(stats.max_depth == 1);
    std::cout << "  -> recursion went straight to the real function: nothing "
                 "measured it, so nothing slowed it down.\n";
#endif

    square(7);
    const auto plain = decltype(square)::snapshot();
    assert(plain.calls == 1 && plain.max_depth == 1);
    std::cout << "\nnon-recursive square(): calls = " << plain.calls
              << ", depth = " << plain.max_depth << " (always 1)\n";

    // The depth column appears only when something actually recursed, so an
    // ordinary program still gets exactly the table it always got.
    tempo::report();
#endif
}
