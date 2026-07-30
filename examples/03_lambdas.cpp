// 03 — Lambdas and functors
//
// Function<> and Method<> take a *pointer* as a template argument, and a
// closure cannot be one. Callable objects go through Functor<F> instead, which
// holds the object itself. Since you cannot name a closure type, these come as
// factories rather than macros:
//
//   tempo::wrap(f)     counting
//   tempo::profile(f)  counting + call-site reporting, via TEMPO_PROFILE_CALL
//   tempo::measure(f)  counting + timing + fastest/slowest arguments

#include "tempo.hpp"

#include <iostream>

int main() {
    int offset = 100;
    auto shift = tempo::wrap([offset](int value) { return value + offset; });
    shift(1);
    shift(2);
    std::cout << "captured offset, " << shift.call_count << " calls\n";

    auto spin = tempo::measure([](int rounds, int id) {
        // Unsigned: the running sum passes INT_MAX well before the loop ends
        // and signed overflow is undefined. Burning time is all this is for.
        volatile unsigned int sink = 0;
        for (int i = 0; i < rounds * 100000; ++i) {
            sink = sink + static_cast<unsigned int>(i);
        }
        return id;
    });

    // Ordinary calls. A lambda has a concrete signature, so the wrapper's
    // operator() is built from it and the call site is captured without a macro
    // -- TEMPO_METRICS_CALL is only needed for member functions.
    spin(3, 301);
    spin(12, 302);
    spin(1, 303);

    const auto [slowest_rounds, slowest_id] = spin.slowest_args();
    std::cout << "slowest call : rounds=" << slowest_rounds
              << " id=" << slowest_id << "\n";

    // A generic lambda -- [](auto x){...} -- has no signature until it is
    // called, so tempo rejects it with a constraint error instead of guessing.
}
