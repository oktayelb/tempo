// 07 — Lambdas, functors and std::function
//
// Function<> and Method<> take a *pointer* as a template argument. A lambda
// cannot be one: a capturing lambda is an object with state, and objects are
// not non-type template parameters. So callable objects get their own wrapper,
// Functor<F>, which is templated on the type and holds the object itself.
//
// Because you cannot name a closure type, use the factories instead of a macro:
//   tempo::wrap(f)     -> call counting
//   tempo::profile(f)  -> counting + call-site reporting
//   tempo::measure(f)  -> counting + timing + fastest/slowest arguments

#include "tempo.hpp"

#include <cassert>
#include <functional>
#include <iostream>
#include <string>

// An ordinary functor: state in a member, const operator().
struct Scale {
    int factor;
    int operator()(int value) const { return value * factor; }
};

// A functor that mutates itself: non-const operator().
struct Accumulate {
    int total = 0;
    int operator()(int value) { return total += value; }
};

int main() {
    // --- what counts as a callable object? ----------------------------------
    auto generic = [](auto value) { return value; };
    static_assert(tempo::CallableObject<Scale>);
    static_assert(tempo::CallableObject<Accumulate>);
    static_assert(tempo::CallableObject<std::function<int(int)>>);

    // A generic lambda has no signature until it is called, so tempo rejects it
    // rather than pretending. tempo::wrap(generic) is a clean constraint error.
    static_assert(!tempo::CallableObject<decltype(generic)>);
    (void)generic;

    // --- a plain lambda, fully introspected ---------------------------------
    auto add = [](int a, int b) { return a + b; };
    using Add = tempo::Functor<decltype(add)>;
    static_assert(std::is_same_v<Add::ReturnType, int>);
    static_assert(std::is_same_v<Add::ArgsType, std::tuple<int, int>>);
    static_assert(Add::arg_count == 2);
    static_assert(Add::is_functor);
    static_assert(!Add::is_member);

    auto counted_add = tempo::wrap(add);
    assert(counted_add(20, 22) == 42);
    assert(counted_add(1, 2) == 3);
    std::cout << "lambda            : " << counted_add(20, 22)
              << "   arg_count=" << Add::arg_count
              << "  calls=" << Add::call_count << "\n";

    // --- captures are carried along ------------------------------------------
    // Not const, on purpose. A const int with a constant initializer is usable
    // inside the lambda without being captured at all, and Clang says so
    // (-Wunused-lambda-capture) -- which would make this a demonstration of a
    // capture that is not one.
    int offset = 100;
    auto shift = tempo::wrap([offset](int value) { return value + offset; });
    assert(shift(1) == 101);
    std::cout << "capturing lambda  : " << shift(1) << "\n";

    // --- a mutable lambda keeps its state across calls -----------------------
    auto tally = tempo::wrap([running = 0](int step) mutable { return running += step; });
    assert(tally(5) == 5);
    assert(tally(5) == 10);
    std::cout << "mutable lambda    : " << tally(5) << "  (state survived 3 calls)\n";

    // --- functors, const and mutating ----------------------------------------
    auto triple = tempo::wrap(Scale{3});
    assert(triple(14) == 42);

    auto running_total = tempo::wrap(Accumulate{});
    assert(running_total(10) == 10);
    assert(running_total(32) == 42);
    std::cout << "functor (const)   : " << triple(14) << "\n";
    std::cout << "functor (mutating): " << running_total(0) << "\n";

    // --- timing a lambda, and recovering its slowest arguments ---------------
    auto spin = tempo::measure([](int rounds, int id) {
        // unsigned, because the running sum passes INT_MAX well before the loop
        // ends and signed overflow is undefined behaviour. Unsigned wraparound
        // is defined, and burning time is all this loop is for.
        volatile unsigned int sink = 0;
        for (int i = 0; i < rounds * 100000; ++i) { sink = sink + static_cast<unsigned int>(i); }
        return id;
    });

    TEMPO_METRICS_CALL(spin, 3, 301);
    TEMPO_METRICS_CALL(spin, 12, 302);
    TEMPO_METRICS_CALL(spin, 1, 303);

    const auto slowest = spin.get_maximizers();
    const auto fastest = spin.get_minimizers();
    std::cout << "\nlambda metrics\n";
    std::cout << "  calls        : " << decltype(spin)::call_count << "\n";
    std::cout << "  slowest args : rounds=" << std::get<0>(slowest)
              << " id=" << std::get<1>(slowest) << "\n";
    std::cout << "  fastest args : rounds=" << std::get<0>(fastest)
              << " id=" << std::get<1>(fastest) << "\n";
    assert(std::get<1>(slowest) == 302);
    assert(std::get<1>(fastest) == 303);

    // --- std::function works, but read the caveat ----------------------------
    std::function<int(int, int)> multiply = [](int a, int b) { return a * b; };
    auto counted_multiply = tempo::wrap(multiply);
    assert(counted_multiply(6, 7) == 42);
    std::cout << "\nstd::function     : " << counted_multiply(6, 7) << "\n";

    // Counters are static per *wrapper type*. Every lambda expression has its
    // own unique closure type, so each lambda gets its own counter -- but all
    // std::function<int(int,int)> objects in the program share ONE type, and
    // therefore one counter. Two unrelated callables get merged:
    std::function<int(int, int)> subtract = [](int a, int b) { return a - b; };
    auto counted_subtract = tempo::wrap(subtract);
    counted_subtract(10, 4);
    std::cout << "shared counter    : "
              << tempo::Functor<std::function<int(int, int)>>::call_count
              << " calls attributed to std::function<int(int,int)>\n";
    std::cout << "                    (multiply and subtract are counted together --\n";
    std::cout << "                     prefer wrapping the lambda directly)\n";

    std::cout << "\nAll callable objects handled.\n";
}
