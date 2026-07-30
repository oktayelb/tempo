// 04 — Timings, extremes, and snapshot consistency
//
// Nothing here asserts an absolute duration: CI machines are shared and noisy.
// What is asserted are the invariants that must hold no matter how slow the
// machine is -- min <= average <= max, total >= max, the recorded arguments
// belonging to the calls that actually produced the extremes.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <thread>

namespace {

int sleep_ms(int milliseconds, int tag) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return tag;
}

int cheap(int a, int b) { return a + b; }

int describe(const std::string& label, int value) {
    return static_cast<int>(label.size()) + value;
}

struct Service {
    int handle(int milliseconds, int tag) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        return tag;
    }
    int inspect(int value) const { return value; }
};

}  // namespace

TEST(extremes_track_the_calls_that_produced_them) {
    using Metrics = TEMPO_CALLABLE_METRICS(sleep_ms);
    Metrics::reset();
    Metrics metrics;

    metrics(12, 101);
    metrics(2, 102);     // fastest
    metrics(25, 103);    // slowest

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 3u);
    CHECK_EQ(stats.timed_calls, 3u);
    CHECK(stats.has_samples);

    // The identifying tag of the fastest and slowest calls, which is the whole
    // point of the feature.
    CHECK_EQ(std::get<1>(stats.min_args), 102);
    CHECK_EQ(std::get<1>(stats.max_args), 103);
    CHECK_EQ(std::get<0>(stats.min_args), 2);
    CHECK_EQ(std::get<0>(stats.max_args), 25);
}

TEST(duration_invariants_hold) {
    using Metrics = TEMPO_CALLABLE_METRICS(sleep_ms);
    Metrics::reset();
    Metrics metrics;

    metrics(5, 1);
    metrics(1, 2);
    metrics(10, 3);

    const auto stats = Metrics::snapshot();
    CHECK_GT(stats.min_duration.count(), 0.0);
    CHECK_LE(stats.min_duration.count(), stats.max_duration.count());
    CHECK_LE(stats.min_duration.count(), stats.average_ms());
    CHECK_LE(stats.average_ms(), stats.max_duration.count());
    CHECK_GE(stats.total_duration.count(), stats.max_duration.count());

    // total should be the sum of the parts, so with three calls it is at least
    // min + max and at most the whole thing.
    CHECK_GE(stats.total_duration.count(),
             stats.min_duration.count() + stats.max_duration.count());
    CHECK_NEAR(stats.average_ms(), stats.total_duration.count() / 3.0, 1e-9);
}

TEST(a_single_call_is_its_own_minimum_and_maximum) {
    using Metrics = TEMPO_CALLABLE_METRICS(sleep_ms);
    Metrics::reset();
    Metrics metrics;

    metrics(4, 55);

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 1u);
    CHECK_EQ(stats.min_duration.count(), stats.max_duration.count());
    CHECK_EQ(std::get<1>(stats.min_args), 55);
    CHECK_EQ(std::get<1>(stats.max_args), 55);
    CHECK_NEAR(stats.average_ms(), stats.total_duration.count(), 1e-9);
}

TEST(fastest_and_slowest_args_agree_with_the_snapshot) {
    using Metrics = TEMPO_CALLABLE_METRICS(sleep_ms);
    Metrics::reset();
    Metrics metrics;

    metrics(8, 201);
    metrics(1, 202);

    const auto stats = Metrics::snapshot();
    CHECK_EQ(std::get<1>(metrics.fastest_args()), std::get<1>(stats.min_args));
    CHECK_EQ(std::get<1>(metrics.slowest_args()), std::get<1>(stats.max_args));
    CHECK_EQ(std::get<1>(metrics.fastest_args()), 202);
    CHECK_EQ(std::get<1>(metrics.slowest_args()), 201);
}

TEST(non_trivial_argument_types_are_stored_by_value) {
    using Metrics = TEMPO_CALLABLE_METRICS(describe);
    Metrics::reset();
    Metrics metrics;

    {
        // A string that dies before we read the snapshot. Storage must be a
        // decayed copy, not a dangling reference into the caller's frame.
        std::string temporary = "a string that goes away";
        metrics(temporary, 1);
    }

    const auto stats = Metrics::snapshot();
    CHECK_EQ(std::get<0>(stats.max_args), std::string{"a string that goes away"});
}

TEST(member_functions_are_timed_and_exclude_the_instance_from_args) {
    using Metrics = TEMPO_CALLABLE_METRICS(Service::handle);
    Metrics::reset();
    Metrics metrics;

    Service service;
    metrics(service, 6, 301);
    metrics(service, 1, 302);

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 2u);
    CHECK_EQ(std::tuple_size_v<Metrics::StoredArgsType>, 2u);
    CHECK_EQ(std::get<1>(stats.min_args), 302);
    CHECK_EQ(std::get<1>(stats.max_args), 301);
}

TEST(const_member_functions_work_through_a_const_instance) {
    using Metrics = TEMPO_CALLABLE_METRICS(Service::inspect);
    Metrics::reset();
    Metrics metrics;

    const Service frozen;
    CHECK_EQ(metrics(frozen, 9), 9);
    CHECK_EQ(Metrics::snapshot().calls, 1u);
}

TEST(report_lists_called_metrics_and_skips_uncalled_ones) {
    tempo::report::reset_all();

    using Called = TEMPO_CALLABLE_METRICS(cheap);
    Called called;
    called(1, 2);

    std::ostringstream out;
    tempo::report::print(out);
    const std::string text = out.str();

    CHECK(text.find("tempo report") != std::string::npos);
    CHECK(text.find("cheap") != std::string::npos);
    CHECK(text.find("calls") != std::string::npos);

    // No recursion happened, so the depth column must not appear -- a program
    // that does not recurse gets exactly the table it always got.
    CHECK(text.find("depth") == std::string::npos);
}

TEST(report_with_nothing_recorded_says_so) {
    tempo::report::reset_all();

    std::ostringstream out;
    tempo::report::print(out);
    CHECK(out.str().find("no calls recorded") != std::string::npos);
}

TEST(snapshot_is_internally_consistent) {
    using Metrics = TEMPO_CALLABLE_METRICS(cheap);
    Metrics::reset();
    Metrics metrics;

    for (int i = 0; i < 50; ++i) { metrics(i, i); }

    // One lock, one coherent view: calls, total and the extremes all describe
    // the same moment, so these relationships cannot be broken by a race.
    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 50u);
    CHECK_EQ(stats.timed_calls, 50u);
    CHECK_GE(stats.total_duration.count(), stats.max_duration.count());
    CHECK_LE(stats.min_duration.count(), stats.max_duration.count());
    CHECK_EQ(stats.max_depth, 1u);
}

TEST(last_call_location_updates) {
    using Metrics = TEMPO_CALLABLE_METRICS(cheap);
    Metrics::reset();
    Metrics metrics;

    metrics(1, 1);
    const auto first = Metrics::snapshot().last_call_location.line();
    metrics(2, 2);
    const auto second = Metrics::snapshot().last_call_location.line();

    CHECK_NE(first, second);
    CHECK_EQ(Metrics::get_last_call_location().line(), second);
}

TEST(void_returning_callables_are_measured) {
    static int side_effect = 0;
    struct Local { static void bump(int by) { side_effect += by; } };

    using Metrics = tempo::Metrics<tempo::Callable<&Local::bump>>;
    Metrics::reset();
    Metrics metrics;

    metrics(3);
    metrics(4);

    CHECK_EQ(side_effect, 7);
    CHECK_EQ(Metrics::snapshot().calls, 2u);
    CHECK(Metrics::snapshot().has_samples);
}
