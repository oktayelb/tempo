// 07 — Counting object construction
//
// ConstructorProfiler forwards its arguments straight into the constructor and
// returns the new object as a prvalue, so it is built directly in the caller's
// storage: no temporary, no copy, no move. Which also means it works for types
// that cannot be copied or moved at all.

#include "tempo.hpp"

#include <iostream>
#include <string>
#include <utility>

struct Payload {
    std::string name;
    explicit Payload(std::string value) : name(std::move(value)) {}
};

struct Pinned {
    int value;
    explicit Pinned(int a, int b) : value(a * b) {}
    Pinned(const Pinned&) = delete;
    Pinned(Pinned&&) = delete;
};

int main() {
    tempo::ConstructorProfiler<Payload> make_payload;

    Payload first = make_payload(std::string{"alpha"});
    Payload second = make_payload("beta");

    std::cout << "built " << first.name << " and " << second.name << ": "
              << make_payload.obj_count << " objects\n";

    // Neither copyable nor movable, and still built in place.
    tempo::ConstructorProfiler<Pinned> make_pinned;
    std::cout << "pinned value : " << make_pinned(6, 7).value << "\n";

    // Whether a constructor matches is a compile-time question, so you can ask
    // it -- and passing arguments no constructor accepts is a named error rather
    // than a wall of overload-resolution noise.
    static_assert(make_payload.can_construct<const char*>);
    static_assert(!make_payload.can_construct<int, int>);

    // This counts objects made *through the profiler*. A plain Payload p{"x"}
    // elsewhere is invisible to it, and so are copies and destructions. A
    // constructor that throws is not counted.
}
