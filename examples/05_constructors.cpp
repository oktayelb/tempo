// 05 — Counting object construction
//
// ConstructorProfiler forwards its arguments straight into the constructor and
// returns the new object as a prvalue, so the object is built directly in the
// caller's storage. No temporary, no copy, no move -- which also means it works
// for types that cannot be copied or moved at all.

#include "tempo.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

struct Payload {
    static inline int copies = 0;
    static inline int moves = 0;

    std::string name;

    explicit Payload(std::string value) : name(std::move(value)) {}
    Payload(const Payload& other) : name(other.name) { ++copies; }
    Payload(Payload&& other) noexcept : name(std::move(other.name)) { ++moves; }
};

struct Pinned {
    int value;
    explicit Pinned(int a, int b) : value(a * b) {}
    Pinned(const Pinned&) = delete;
    Pinned(Pinned&&) = delete;
};

struct Fragile {
    explicit Fragile(bool should_throw) {
        if (should_throw) {
            throw std::runtime_error("constructor failed");
        }
    }
};

int main() {
    // --- asking, at compile time, whether a constructor matches ---------------
    using PayloadProfiler = tempo::ConstructorProfiler<Payload>;
    using PinnedProfiler = tempo::ConstructorProfiler<Pinned>;

    static_assert(PayloadProfiler::can_construct<std::string>);
    static_assert(PayloadProfiler::can_construct<const char*>);
    static_assert(!PayloadProfiler::can_construct<int, int>);
    static_assert(PinnedProfiler::can_construct<int, int>);
    static_assert(!PinnedProfiler::can_construct<int>);
    static_assert(!PinnedProfiler::can_construct<>);

    // Calling with arguments no constructor accepts is a compile error with a
    // named cause, not a wall of overload-resolution noise:
    //   make_payload(1, 2);
    // -> static assertion failed: tempo::ConstructorProfiler: ClassType bu
    //    argumanlarla kurulamiyor...

    std::cout << "can_construct<std::string> : " << PayloadProfiler::can_construct<std::string> << "\n";
    std::cout << "can_construct<int, int>    : " << PayloadProfiler::can_construct<int, int> << "\n";

    // --- construction is counted ---------------------------------------------
    tempo::ConstructorProfiler<Payload> make_payload;

    Payload::copies = Payload::moves = 0;
    Payload first = make_payload(std::string{"alpha"});
    Payload second = make_payload(std::string{"beta"});

    assert(first.name == "alpha");
    assert(second.name == "beta");
    assert(decltype(make_payload)::obj_count == 2);

    std::cout << "constructed  : " << decltype(make_payload)::obj_count << " objects\n";
    std::cout << "copies/moves : " << Payload::copies << " / " << Payload::moves
              << "  (the profiler itself adds neither)\n";

    // --- arguments are forwarded, so an lvalue is copied and an rvalue moved --
    // Here the string is an lvalue, so Payload's constructor copies it once --
    // exactly what a direct `Payload p{text}` would have done.
    std::string text = "gamma";
    Payload third = make_payload(text);
    assert(third.name == "gamma");
    assert(text == "gamma");  // untouched: we passed an lvalue, nothing moved from it
    std::cout << "lvalue argument left intact: \"" << text << "\"\n";

    // --- types that can be neither copied nor moved --------------------------
    tempo::ConstructorProfiler<Pinned> make_pinned;
    Pinned pinned = make_pinned(6, 7);
    assert(pinned.value == 42);
    std::cout << "\nnon-copyable, non-movable type built in place: "
              << pinned.value << "\n";
    std::cout << "count        : " << decltype(make_pinned)::obj_count << "\n";

    // --- a throwing constructor is not counted -------------------------------
    tempo::ConstructorProfiler<Fragile> make_fragile;
    Fragile ok = make_fragile(false);
    (void)ok;
    try {
        Fragile bad = make_fragile(true);
        (void)bad;
    }
    catch (const std::runtime_error&) {
        std::cout << "\ncaught the failing constructor\n";
    }
    assert(decltype(make_fragile)::obj_count == 1);
    std::cout << "count after 1 success + 1 throw: "
              << decltype(make_fragile)::obj_count << "\n";

    // Worth knowing: this counts objects made *through the profiler*. A plain
    // `Payload p{"delta"}` elsewhere in your program is invisible to it, and so
    // are copies, moves and destructions.
}
