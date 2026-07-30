// 01 — Counting calls
//
// The smallest thing tempo does. Wrap a function and every call through the
// wrapper is counted. Callable<> accepts a free function or a method, so
// downstream code never has to care which it got -- a method simply takes its
// instance as the first argument.

#include "tempo.hpp"

#include <iostream>

int add(int a, int b) { return a + b; }

struct Cache {
    int hits = 0;
    int lookup(int key) { return ++hits + key; }
};

int main() {
    TEMPO_CALLABLE(add) counted_add;
    TEMPO_CALLABLE(Cache::lookup) counted_lookup;

    Cache cache;
    for (int i = 0; i < 3; ++i) { counted_add(i, i); }
    for (int i = 0; i < 5; ++i) { counted_lookup(cache, i); }

    // The counter belongs to the wrapped function, not to the wrapper object:
    // it is a static member of Callable<&add>, so two wrappers over the same
    // function share one count.
    std::cout << "add    : " << decltype(counted_add)::call_count << " calls\n";
    std::cout << "lookup : " << decltype(counted_lookup)::call_count << " calls\n";
}
