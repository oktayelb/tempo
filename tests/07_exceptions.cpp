// 07 — Exception safety
//
// tempo does its recording in destructors, which run during stack unwinding as
// well as on normal return. Every one of them compares uncaught_exceptions()
// against its value on entry to tell the two apart. These tests make sure a
// throwing call cannot corrupt the statistics, and that state which must be
// unwound anyway -- the recursion depth -- always is.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Boom : std::runtime_error {
    Boom() : std::runtime_error("boom") {}
};

int always_throws(int) { throw Boom{}; }

int throws_on_negative(int value) {
    if (value < 0) { throw Boom{}; }
    return value;
}

struct Service {
    int fail(int) { throw Boom{}; }
};

struct ThrowingConstructor {
    int value;
    explicit ThrowingConstructor(int v) : value(v) {
        if (v < 0) { throw Boom{}; }
    }
};

struct CountedConstruction {
    static inline int live = 0;
    CountedConstruction() { ++live; }
    CountedConstruction(const CountedConstruction&) { ++live; }
    ~CountedConstruction() { --live; }
};

}  // namespace

TEST(a_throwing_call_records_no_duration) {
    using Metrics = TEMPO_CALLABLE_METRICS(always_throws);
    Metrics::reset();
    Metrics metrics;

    CHECK_THROWS_AS(metrics(1), Boom);

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.timed_calls, 0u);
    CHECK(!stats.has_samples);
    CHECK_EQ(stats.total_duration.count(), 0.0);
    CHECK_EQ(stats.max_duration.count(), 0.0);
    CHECK_EQ(stats.average_ms(), 0.0);
}

TEST(a_throwing_call_is_still_counted) {
    using Metrics = TEMPO_CALLABLE_METRICS(always_throws);
    Metrics::reset();
    Metrics metrics;

    CHECK_THROWS_AS(metrics(1), Boom);
    CHECK_THROWS_AS(metrics(2), Boom);

    // The count is incremented by the wrapper before the call, so it reflects
    // attempts. Only the timing is withheld.
    CHECK_EQ(Metrics::snapshot().calls, 2u);
}

TEST(statistics_survive_a_throw_in_the_middle) {
    using Metrics = TEMPO_CALLABLE_METRICS(throws_on_negative);
    Metrics::reset();
    Metrics metrics;

    metrics(1);
    CHECK_THROWS_AS(metrics(-1), Boom);
    metrics(2);

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 3u);        // all three attempts
    CHECK_EQ(stats.timed_calls, 2u);  // only the two that returned
    CHECK(stats.has_samples);
    CHECK_GT(stats.total_duration.count(), 0.0);

    // The failed call must not have poisoned the recorded arguments.
    CHECK_GE(std::get<0>(stats.max_args), 1);
    CHECK_GE(std::get<0>(stats.min_args), 1);
}

TEST(a_throwing_member_function_behaves_the_same) {
    using Metrics = TEMPO_CALLABLE_METRICS(Service::fail);
    Metrics::reset();
    Metrics metrics;
    Service service;

    CHECK_THROWS_AS(metrics(service, 1), Boom);

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 1u);
    CHECK_EQ(stats.timed_calls, 0u);
    CHECK(!stats.has_samples);
}

TEST(a_throwing_lambda_behaves_the_same) {
    auto metrics = tempo::measure([](int x) -> int {
        if (x < 0) { throw Boom{}; }
        return x;
    });
    decltype(metrics)::reset();

    metrics(5);
    CHECK_THROWS_AS(metrics(-5), Boom);
    metrics(6);

    const auto stats = decltype(metrics)::snapshot();
    CHECK_EQ(stats.calls, 3u);
    CHECK_EQ(stats.timed_calls, 2u);
}

TEST(profiler_survives_a_throw) {
    using Profiler = TEMPO_CALLABLE_PROFILER(always_throws);
    Profiler profiler;

    CHECK_THROWS_AS(TEMPO_PROFILE_CALL(profiler, 1), Boom);
    // The important part is that the destructor did not itself throw, which
    // would terminate. Getting here at all is the assertion.
    CHECK(true);
}

TEST(constructor_profiler_does_not_count_a_failed_construction) {
    using Maker = tempo::ConstructorProfiler<ThrowingConstructor>;
    Maker::obj_count = 0;
    Maker make;

    CHECK_EQ(make(5).value, 5);
    CHECK_EQ(Maker::obj_count.load(), 1u);

    CHECK_THROWS_AS(make(-1), Boom);
    // The object was never constructed, so it must not be counted.
    CHECK_EQ(Maker::obj_count.load(), 1u);

    CHECK_EQ(make(7).value, 7);
    CHECK_EQ(Maker::obj_count.load(), 2u);
}

TEST(constructor_profiler_counts_only_successful_constructions) {
    using Maker = tempo::ConstructorProfiler<ThrowingConstructor>;
    Maker::obj_count = 0;
    Maker make;

    int succeeded = 0;
    for (int i = -5; i <= 5; ++i) {
        try {
            (void)make(i);
            ++succeeded;
        } catch (const Boom&) {
        }
    }

    CHECK_EQ(succeeded, 6);                          // 0 through 5
    CHECK_EQ(Maker::obj_count.load(), 6u);
}

TEST(no_objects_are_leaked_when_a_call_throws) {
    CountedConstruction::live = 0;
    {
        using Metrics = TEMPO_CALLABLE_METRICS(throws_on_negative);
        Metrics::reset();
        Metrics metrics;
        CHECK_THROWS_AS(metrics(-1), Boom);
    }
    CHECK_EQ(CountedConstruction::live, 0);
}

TEST(nested_throws_do_not_confuse_the_uncaught_count) {
    // A call made while another exception is already in flight. The recorder
    // compares against its own entry value rather than testing for zero, so it
    // must still tell "I threw" from "someone else was already throwing".
    using Metrics = TEMPO_CALLABLE_METRICS(throws_on_negative);
    Metrics::reset();
    Metrics metrics;

    struct CallsDuringUnwind {
        Metrics& metrics;
        ~CallsDuringUnwind() {
            // Runs while a Boom is propagating. This call SUCCEEDS, and must be
            // recorded as a success despite the in-flight exception.
            metrics(42);
        }
    };

    try {
        CallsDuringUnwind guard{metrics};
        throw Boom{};
    } catch (const Boom&) {
    }

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 1u);
    CHECK_EQ(stats.timed_calls, 1u);   // the nested call returned normally
    CHECK(stats.has_samples);
    CHECK_EQ(std::get<0>(stats.max_args), 42);
}

TEST(reset_after_a_throw_leaves_clean_state) {
    using Metrics = TEMPO_CALLABLE_METRICS(always_throws);
    Metrics::reset();
    Metrics metrics;

    CHECK_THROWS_AS(metrics(1), Boom);
    Metrics::reset();

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 0u);
    CHECK_EQ(stats.timed_calls, 0u);
    CHECK_EQ(stats.max_depth, 0u);
    CHECK(!stats.has_samples);
}
