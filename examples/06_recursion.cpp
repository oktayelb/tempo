// 06 — Recursion
//
// A recursive call is resolved at compile time to the function itself, so no
// library can intercept it: the body has to name the wrapper. TEMPO_RECURSIVE
// sets that up in one line and TEMPO_SELF inside the body is the switch.
//
//   TEMPO_COUNT_RECURSION=0 (default)  recursive calls go straight to the real
//                                      function: nothing measures them, so
//                                      nothing slows them down.
//   TEMPO_COUNT_RECURSION=1            recursive calls go through the wrapper
//                                      and every one of them is counted.
//
// Either way only the outermost call is *timed*, so the total tracks the wall
// clock instead of summing intervals that contain one another.
//
//     g++ -std=c++20 -O2 -I.. 06_recursion.cpp -o r0
//     g++ -std=c++20 -O2 -I.. -DTEMPO_COUNT_RECURSION=1 06_recursion.cpp -o r1

#include "tempo.hpp"

#include <iostream>

TEMPO_RECURSIVE(int, fibonacci, unsigned n) {
    return n < 2 ? static_cast<int>(n)
                 : TEMPO_SELF(fibonacci)(n - 1) + TEMPO_SELF(fibonacci)(n - 2);
}

int main() {
    const int result = fibonacci(24);

#if TEMPO_ENABLED
    const auto stats = decltype(fibonacci)::snapshot();

    std::cout << "TEMPO_COUNT_RECURSION = " << TEMPO_COUNT_RECURSION << "\n"
              << "  fibonacci(24)    : " << result << "\n"
              << "  calls counted    : " << stats.calls << "\n"
              << "  outermost calls  : " << stats.timed_calls << "\n"
              << "  deepest recursion: " << stats.max_depth << "\n"
              << "  total time       : " << stats.total_duration.count() << " ms\n";

    // The report grows a depth column only when something actually recursed, so
    // an ordinary program still prints the table it always did.
    tempo::report();
#else
    std::cout << "built with TEMPO_ENABLED=0: fibonacci(24) = " << result
              << ", with tempo out of the build.\n";
#endif
}
