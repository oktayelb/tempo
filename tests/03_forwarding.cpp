// 03 — Forwarding, copies, moves and elision
//
// The claim under test: the wrapper does not add copies or moves of its own on
// the way in, and the return value is never copied or moved at all because the
// call expression is returned directly.
//
// Note that Metrics deliberately DOES copy arguments -- that is the whole
// argument-capture feature -- so the transparency tests use Callable and
// Profiler, and the Metrics costs are pinned separately and honestly.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <memory>
#include <string>
#include <utility>

namespace {

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
};

int by_value(Tracker t) { return t.id; }
int by_const_ref(const Tracker& t) { return t.id; }
int by_ref(Tracker& t) { t.id += 1; return t.id; }
int by_rvalue_ref(Tracker&& t) { return t.id; }

// Neither copyable nor movable. Returning one by value is legal since C++17.
struct Immovable {
    int value;
    explicit Immovable(int v) : value(v) {}
    Immovable(const Immovable&) = delete;
    Immovable(Immovable&&) = delete;
    Immovable& operator=(const Immovable&) = delete;
    Immovable& operator=(Immovable&&) = delete;
};

Immovable make_immovable(int v) { return Immovable(v); }

int consume(std::unique_ptr<int> owned) { return owned ? *owned : -1; }

std::string& identity_ref(std::string& s) { return s; }

}  // namespace

TEST(callable_adds_nothing_for_reference_parameters) {
    using ConstRef = TEMPO_CALLABLE(by_const_ref);
    ConstRef wrapper;

    Tracker value{7};
    Tracker::reset();
    CHECK_EQ(wrapper(value), 7);
    CHECK_EQ(Tracker::copies, 0);
    CHECK_EQ(Tracker::moves, 0);

    Tracker::reset();
    CHECK_EQ(wrapper(Tracker{9}), 9);
    CHECK_EQ(Tracker::copies, 0);
    CHECK_EQ(Tracker::moves, 0);
}

TEST(callable_preserves_value_category_for_by_value_parameters) {
    using ByValue = TEMPO_CALLABLE(by_value);
    ByValue wrapper;

    // An lvalue must be copied exactly once -- by the callee's own parameter,
    // not by the wrapper.
    Tracker lvalue{1};
    Tracker::reset();
    wrapper(lvalue);
    CHECK_EQ(Tracker::copies, 1);
    CHECK_EQ(Tracker::moves, 0);

    // An rvalue must be moved exactly once, never copied.
    Tracker::reset();
    wrapper(Tracker{2});
    CHECK_EQ(Tracker::copies, 0);
    CHECK_EQ(Tracker::moves, 1);

    // An explicitly moved lvalue behaves like an rvalue.
    Tracker movable{3};
    Tracker::reset();
    wrapper(std::move(movable));
    CHECK_EQ(Tracker::copies, 0);
    CHECK_EQ(Tracker::moves, 1);
}

TEST(mutable_reference_parameters_reach_the_callee) {
    using ByRef = TEMPO_CALLABLE(by_ref);
    ByRef wrapper;

    Tracker value{10};
    Tracker::reset();
    CHECK_EQ(wrapper(value), 11);
    CHECK_EQ(value.id, 11);          // the callee really mutated our object
    CHECK_EQ(Tracker::copies, 0);
    CHECK_EQ(Tracker::moves, 0);
}

TEST(rvalue_reference_parameters_bind_without_a_copy) {
    using ByRvalue = TEMPO_CALLABLE(by_rvalue_ref);
    ByRvalue wrapper;

    Tracker::reset();
    CHECK_EQ(wrapper(Tracker{42}), 42);
    CHECK_EQ(Tracker::copies, 0);
    CHECK_EQ(Tracker::moves, 0);
}

TEST(move_only_arguments_pass_through) {
    using Consume = TEMPO_CALLABLE(consume);
    Consume wrapper;
    CHECK_EQ(wrapper(std::make_unique<int>(5)), 5);
}

TEST(immovable_return_types_pass_through_every_layer) {
    // Returning the call expression directly means guaranteed copy elision
    // applies, so a type that can be neither copied nor moved still works.
    using Make = TEMPO_CALLABLE(make_immovable);
    Make wrapper;
    CHECK_EQ(wrapper(11).value, 11);

    using Profiler = TEMPO_CALLABLE_PROFILER(make_immovable);
    Profiler profiler;
    CHECK_EQ(profiler(12).value, 12);

    using Metrics = TEMPO_CALLABLE_METRICS(make_immovable);
    Metrics metrics;
    CHECK_EQ(metrics(13).value, 13);
    CHECK_EQ(metrics(14).value, 14);
}

TEST(reference_returns_stay_references) {
    using Identity = TEMPO_CALLABLE(identity_ref);
    Identity wrapper;

    std::string text = "hello";
    std::string& result = wrapper(text);
    CHECK_EQ(&result, &text);        // same object, not a copy
    result += "!";
    CHECK_EQ(text, std::string{"hello!"});
}

TEST(metrics_argument_capture_costs_are_what_they_are) {
    // Metrics copies arguments on purpose, to remember the fastest and slowest
    // inputs. This test does not pretend that is free; it pins the cost so a
    // regression that adds another copy is visible.
    using Metrics = TEMPO_CALLABLE_METRICS(by_const_ref);
    Metrics::reset();
    Metrics metrics;

    Tracker value{1};
    Tracker::reset();
    metrics(value);

    // First call: one copy into the snapshot, then the snapshot is shared
    // between min and max (one copy, one move).
    CHECK_LE(Tracker::copies, 2);
    CHECK_LE(Tracker::moves, 1);
    CHECK_EQ(std::get<0>(Metrics::snapshot().max_args).id, 1);

    // A later call that is neither a new min nor a new max still snapshots, but
    // must not grow without bound.
    Tracker::reset();
    for (int i = 0; i < 5; ++i) { metrics(value); }
    CHECK_LE(Tracker::copies, 15);
}

TEST(metrics_records_arguments_from_before_the_call_consumed_them) {
    // The snapshot is taken before forwarding, by copy. If it were taken after,
    // or by forwarding to both places, we would store moved-from values.
    using Metrics = TEMPO_CALLABLE_METRICS(by_value);
    Metrics::reset();
    Metrics metrics;

    metrics(Tracker{77});

    const auto stored = Metrics::snapshot().max_args;
    CHECK_EQ(std::get<0>(stored).id, 77);   // not -1, which is our moved-from marker
}
