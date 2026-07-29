// 02 — One abstraction over everything callable
//
// Function<> wraps free functions, Method<> wraps member functions, and
// Callable<> picks the right one for you so downstream code never has to care
// which it got. Calls are counted per wrapped entity, at no cost beyond the
// counter itself.

#include "tempo.hpp"

#include <cassert>
#include <iostream>
#include <memory>

int add(int a, int b) { return a + b; }

struct Counter {
    int base = 0;
    int bump(int delta) { return base += delta; }
    int peek(int delta) const { return base + delta; }
};

int main() {
    // --- Callable<> hides the free-function / method distinction -------------
    TEMPO_CALLABLE(add) call_free;
    TEMPO_CALLABLE(Counter::bump) call_method;
    TEMPO_CALLABLE(Counter::peek) call_const_method;

    Counter counter{40};
    const Counter frozen{40};

    assert(call_free(20, 22) == 42);
    assert(call_method(counter, 2) == 42);   // instance is passed as first argument
    assert(call_const_method(frozen, 2) == 42);

    std::cout << "free function  : " << call_free(20, 22) << "\n";
    std::cout << "method         : " << call_method(counter, 0) << "\n";
    std::cout << "const method   : " << call_const_method(frozen, 2) << "\n";

    // --- call_count lives on the wrapped entity, not on the wrapper object ---
    // Two wrapper instances over the same function share one counter, because
    // the counter is a static member of Function<&add>.
    using Add = TEMPO_CALLABLE(add);
    Add::call_count.store(0);
    TEMPO_CALLABLE(add) another_wrapper;
    call_free(1, 1);
    another_wrapper(2, 2);
    std::cout << "\ncall_count after 1 call through each of 2 wrappers: "
              << Add::call_count << "\n";

    // --- methods reach the instance through std::invoke ----------------------
    // That means anything std::invoke understands works: a reference, a raw
    // pointer, a reference_wrapper, or a smart pointer.
    TEMPO_METHOD(Counter::peek) peek;
    Counter object{10};
    auto owned = std::make_shared<Counter>(10);

    assert(peek(object, 1) == 11);
    assert(peek(&object, 2) == 12);
    assert(peek(std::ref(object), 3) == 13);
    assert(peek(owned, 4) == 14);

    std::cout << "\nsame method called through:\n";
    std::cout << "  reference        -> " << peek(object, 1) << "\n";
    std::cout << "  raw pointer      -> " << peek(&object, 2) << "\n";
    std::cout << "  reference_wrapper-> " << peek(std::ref(object), 3) << "\n";
    std::cout << "  shared_ptr       -> " << peek(owned, 4) << "\n";

    // --- constness is enforced by the constraint, not by a runtime check -----
    // The next line would not compile: a const Counter cannot call bump().
    //   call_method(frozen, 1);
    // You get "constraint not satisfied" at this line, not an error deep
    // inside std::invoke.

    std::cout << "\nAll calls dispatched correctly.\n";
}
