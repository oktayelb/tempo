// 09 — ConstructorProfiler
//
// The counter increments in a destructor that runs after the object has been
// constructed, so that the constructor's result can be returned as a prvalue and
// land directly in the caller's storage. That is what lets non-copyable and
// non-movable types work at all, and it is what these tests hold in place.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Plain {
    int a = 0;
    double b = 0.0;
    Plain() = default;
    Plain(int a_, double b_) : a(a_), b(b_) {}
};

struct Immovable {
    int value;
    explicit Immovable(int v) : value(v) {}
    Immovable(const Immovable&) = delete;
    Immovable(Immovable&&) = delete;
};

struct CountsItsOwnCopies {
    static inline int copies = 0;
    static inline int moves = 0;
    int value;

    explicit CountsItsOwnCopies(int v) : value(v) {}
    CountsItsOwnCopies(const CountsItsOwnCopies& other) : value(other.value) { ++copies; }
    CountsItsOwnCopies(CountsItsOwnCopies&& other) noexcept : value(other.value) { ++moves; }

    static void reset() { copies = moves = 0; }
};

struct TakesOwnership {
    std::unique_ptr<int> owned;
    explicit TakesOwnership(std::unique_ptr<int> p) : owned(std::move(p)) {}
};

struct Overloaded {
    std::string tag;
    Overloaded() : tag("default") {}
    explicit Overloaded(int) : tag("int") {}
    Overloaded(int, int) : tag("int,int") {}
    explicit Overloaded(const std::string& s) : tag("string:" + s) {}
};

}  // namespace

TEST(counts_successful_constructions) {
    using Maker = tempo::ConstructorProfiler<Plain>;
    Maker::obj_count = 0;
    Maker make;

    const Plain first = make(1, 2.0);
    const Plain second = make(3, 4.0);

    CHECK_EQ(first.a, 1);
    CHECK_EQ(second.a, 3);
    CHECK_EQ(Maker::obj_count.load(), 2u);
}

TEST(the_counter_is_shared_per_type_not_per_profiler) {
    using Maker = tempo::ConstructorProfiler<Plain>;
    Maker::obj_count = 0;

    Maker first;
    Maker second;
    (void)first(1, 1.0);
    (void)second(2, 2.0);

    CHECK_EQ(Maker::obj_count.load(), 2u);
}

TEST(different_types_count_independently) {
    using PlainMaker = tempo::ConstructorProfiler<Plain>;
    using StringMaker = tempo::ConstructorProfiler<std::string>;
    PlainMaker::obj_count = 0;
    StringMaker::obj_count = 0;

    PlainMaker plain;
    StringMaker text;

    (void)plain(1, 1.0);
    (void)text(std::string{"a"});
    (void)text(std::string{"b"});

    CHECK_EQ(PlainMaker::obj_count.load(), 1u);
    CHECK_EQ(StringMaker::obj_count.load(), 2u);
}

TEST(non_movable_types_are_constructed_in_place) {
    // Returning a prvalue means guaranteed copy elision, so a type that can be
    // neither copied nor moved still works. Binding to a named local and
    // returning it would not compile.
    using Maker = tempo::ConstructorProfiler<Immovable>;
    Maker::obj_count = 0;
    Maker make;

    const Immovable object = make(42);
    CHECK_EQ(object.value, 42);
    CHECK_EQ(Maker::obj_count.load(), 1u);
}

TEST(construction_adds_no_copies_or_moves_of_its_own) {
    using Maker = tempo::ConstructorProfiler<CountsItsOwnCopies>;
    Maker::obj_count = 0;
    Maker make;

    CountsItsOwnCopies::reset();
    const CountsItsOwnCopies object = make(7);

    CHECK_EQ(object.value, 7);
    CHECK_EQ(CountsItsOwnCopies::copies, 0);
    CHECK_EQ(CountsItsOwnCopies::moves, 0);
    CHECK_EQ(Maker::obj_count.load(), 1u);
}

TEST(arguments_are_forwarded_so_move_only_types_work) {
    using Maker = tempo::ConstructorProfiler<TakesOwnership>;
    Maker::obj_count = 0;
    Maker make;

    const TakesOwnership object = make(std::make_unique<int>(9));
    CHECK(object.owned != nullptr);
    CHECK_EQ(*object.owned, 9);
    CHECK_EQ(Maker::obj_count.load(), 1u);
}

TEST(overload_selection_matches_the_arguments) {
    using Maker = tempo::ConstructorProfiler<Overloaded>;
    Maker::obj_count = 0;
    Maker make;

    CHECK_EQ(make().tag, std::string{"default"});
    CHECK_EQ(make(1).tag, std::string{"int"});
    CHECK_EQ(make(1, 2).tag, std::string{"int,int"});
    CHECK_EQ(make(std::string{"x"}).tag, std::string{"string:x"});
    CHECK_EQ(Maker::obj_count.load(), 4u);
}

TEST(can_construct_answers_at_compile_time) {
    using Maker = tempo::ConstructorProfiler<Overloaded>;

    static_assert(Maker::can_construct<>);
    static_assert(Maker::can_construct<int>);
    static_assert(Maker::can_construct<int, int>);
    static_assert(Maker::can_construct<std::string>);
    static_assert(!Maker::can_construct<int, int, int>);
    static_assert(!Maker::can_construct<std::vector<int>>);

    // Immovable cannot be built from nothing.
    static_assert(!tempo::ConstructorProfiler<Immovable>::can_construct<>);
    static_assert(tempo::ConstructorProfiler<Immovable>::can_construct<int>);

    CHECK(true);
}

TEST(counting_many_constructions_stays_exact) {
    using Maker = tempo::ConstructorProfiler<Plain>;
    Maker::obj_count = 0;
    Maker make;

    std::vector<Plain> objects;
    objects.reserve(1000);
    for (int i = 0; i < 1000; ++i) { objects.push_back(make(i, i)); }

    // The profiler counts constructions it performed, not every Plain that ever
    // existed -- vector growth and the push_back copies are not its business.
    CHECK_EQ(Maker::obj_count.load(), 1000u);
    CHECK_EQ(objects.size(), 1000u);
    CHECK_EQ(objects[999].a, 999);
}

TEST(the_profiled_type_can_be_a_standard_container) {
    using Maker = tempo::ConstructorProfiler<std::vector<int>>;
    Maker::obj_count = 0;
    Maker make;

    const auto vec = make(std::size_t{5}, 7);
    CHECK_EQ(vec.size(), 5u);
    CHECK_EQ(vec[0], 7);
    CHECK_EQ(Maker::obj_count.load(), 1u);
}
