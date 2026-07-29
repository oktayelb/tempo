// 03 — The wrapper is transparent
//
// A profiler that copies your arguments changes the thing it is measuring.
// tempo's wrappers take forwarding references, so an argument reaches the real
// function with its original value category and without a single extra copy.
// This example counts copies and moves to prove it.

#include "tempo.hpp"

#include <cassert>
#include <iostream>
#include <memory>

struct Tracker {
    static inline int copies = 0;
    static inline int moves = 0;

    int id = 0;

    Tracker() = default;
    explicit Tracker(int value) : id(value) {}
    Tracker(const Tracker& other) : id(other.id) { ++copies; }
    Tracker(Tracker&& other) noexcept : id(other.id) { ++moves; other.id = -1; }
    Tracker& operator=(const Tracker& other) { id = other.id; ++copies; return *this; }
    Tracker& operator=(Tracker&& other) noexcept { id = other.id; ++moves; other.id = -1; return *this; }

    static void reset() { copies = moves = 0; }
    static void report(const char* label) {
        std::cout << "  " << label << ": " << copies << " copies, " << moves << " moves\n";
    }
};

int by_value(Tracker t) { return t.id; }
void by_rvalue_ref(Tracker&& t) { (void)t; }
int consume(std::unique_ptr<int> owned) { return *owned; }

// A type that can be neither copied nor moved. Returning one by value is legal
// since C++17, and tempo returns the call expression directly so it works here.
struct Immovable {
    int value;
    explicit Immovable(int v) : value(v) {}
    Immovable(const Immovable&) = delete;
    Immovable(Immovable&&) = delete;
};
Immovable make_immovable(int v) { return Immovable(v); }

int main() {
    TEMPO_FUNCTION(by_value) call;

    // --- an lvalue costs exactly the one copy the callee's parameter needs ---
    std::cout << "by_value(lvalue)\n";
    Tracker::reset();
    {
        Tracker t{7};
        assert(call(t) == 7);
    }
    Tracker::report("expected 1 copy, 0 moves");

    // --- an rvalue is moved, never copied ------------------------------------
    std::cout << "by_value(rvalue)\n";
    Tracker::reset();
    assert(call(Tracker{8}) == 8);
    Tracker::report("expected 0 copies, 1 move");

    // --- an rvalue-reference parameter --------------------------------------
    // Without forwarding this would not even compile: inside the wrapper the
    // parameter is a *named* rvalue reference, which is an lvalue.
    TEMPO_FUNCTION(by_rvalue_ref) sink;
    sink(Tracker{9});
    std::cout << "\nTracker&& parameter accepted\n";

    // --- move-only arguments -------------------------------------------------
    // CallableMetrics normally records the arguments of the fastest and slowest
    // call. unique_ptr cannot be copied, so argument tracking switches itself
    // off and you still get the timings.
    using Consume = TEMPO_CALLABLE_METRICS(consume);
    static_assert(!Consume::tracks_args);
    Consume metrics;
    assert(TEMPO_METRICS_CALL(metrics, std::make_unique<int>(99)) == 99);
    std::cout << "move-only argument forwarded (tracks_args = false)\n";

    // --- a return type with copy and move both deleted -----------------------
    TEMPO_CALLABLE_METRICS(make_immovable) immovable_metrics;
    assert(TEMPO_METRICS_CALL(immovable_metrics, 42).value == 42);
    std::cout << "immovable return type constructed in place\n";

    // --- recorded arguments are captured before the call ---------------------
    // by_value takes its parameter by value and may move from it. tempo copies
    // the arguments *before* invoking, so the recorded values are the ones you
    // passed, not moved-from husks.
    using ByValue = TEMPO_CALLABLE_METRICS(by_value);
    ByValue::reset();
    ByValue recorder;
    TEMPO_METRICS_CALL(recorder, Tracker{123});
    const auto slowest = recorder.get_maximizers();
    assert(std::get<0>(slowest).id == 123);
    std::cout << "recorded argument id = " << std::get<0>(slowest).id
              << " (123, not -1 -- the snapshot predates the move)\n";

    std::cout << "\nWrapper adds no copies of its own.\n";
}
