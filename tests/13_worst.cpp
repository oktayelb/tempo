// 13 — The ranking of the slowest calls
//
// A metric keeps the N slowest calls it has seen, arguments included, so the
// worst input is a pattern rather than a single outlier that might have been a
// cold cache. As everywhere else in this suite, nothing asserts an absolute
// duration: the ordering is forced by sleeping for durations far enough apart
// that a noisy machine cannot reorder them, and everything else is structural.
//
// The arity cases matter most here, because the ranking stores a tuple whose
// size is whatever the callable's parameter list is: zero parameters, many
// parameters, a member function whose instance must not be stored, and a
// callable whose arguments cannot be captured at all.
//
// The ordering is forced with busy_ms below rather than with a sleep; see the
// note there for why a sleeping call cannot rank reliably on a loaded machine.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

// Occupies the CPU for a given number of milliseconds instead of sleeping.
//
// The ordering tests force a ranking by making one call take longer than
// another, and a sleeping call is the wrong tool for that: the thread has to be
// woken again, and on a loaded hosted runner that wake-up latency is unbounded.
// Spinning never asks to be rescheduled, so the floor is the loop rather than
// the scheduler.
//
// steady_clock::now() is an opaque call, so the loop cannot be optimised away.
int busy_ms(int milliseconds, int tag) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
    }
    return tag;
}

int cheap(int a, int b) { return a + b; }

int nullary() { return 7; }

// Sixteen parameters, to prove the stored tuple is not arity-limited.
int wide(int a1, int, int, int, int, int, int, int,
         int, int, int, int, int, int, int, int a16) {
    return a1 + a16;
}

int consume(std::unique_ptr<int> value) { return value ? *value : 0; }

struct Service {
    int handle(int milliseconds, int tag) { return busy_ms(milliseconds, tag); }
};

// The tag of each ranked call, slowest first.
template <typename Metrics>
std::vector<int> ranked_tags() {
    std::vector<int> tags;
    for (const auto& entry : Metrics::worst_calls()) {
        tags.push_back(std::get<1>(entry.args));
    }
    return tags;
}

// Remembers how long the CALLER saw each call take, so the expected ranking can
// be derived from the durations that actually happened rather than from the
// milliseconds that were asked for.
//
// That distinction is the whole point. Spinning sets a floor, not a ceiling: on
// a busy machine the thread is simply not running for part of a call, and a
// call asked for 3 ms can finish after one asked for 10 ms. When that happens
// the ranking tempo produced is still correct -- it is the request that stopped
// describing what happened, and an expectation written as {40, 20, 10} fails
// while nothing is wrong. Timing from the outside keeps the expectation and the
// measurement together: a stall inside the call inflates both alike, so the two
// disagree only over the sliver of wrapper bookkeeping outside tempo's own
// timer, which is microseconds against gaps of milliseconds.
struct Observer {
    using Duration = std::chrono::duration<double, std::milli>;

    struct Call {
        int tag;
        Duration duration;
    };

    std::vector<Call> calls;

    // Times an arbitrary invocation -- used where the metric is a member and
    // the instance has to come first.
    template <typename Invoke>
    void call(int tag, Invoke&& invoke) {
        const auto start = std::chrono::steady_clock::now();
        invoke();
        calls.push_back({tag, std::chrono::steady_clock::now() - start});
    }

    // The common case: a metric over busy_ms's own (milliseconds, tag).
    template <typename Metrics>
    void call(Metrics& metrics, int milliseconds, int tag) {
        call(tag, [&] { metrics(milliseconds, tag); });
    }

    // The tags of the `count` slowest calls, slowest first. stable_sort matches
    // how tempo breaks a tie: rank_worst inserts on a strict >, so a call that
    // ties an existing entry lands after it, in call order.
    std::vector<int> slowest(std::size_t count) const {
        std::vector<Call> ordered = calls;
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const Call& left, const Call& right) {
                             return left.duration > right.duration;
                         });
        ordered.resize(std::min(count, ordered.size()));

        std::vector<int> tags;
        for (const Call& entry : ordered) { tags.push_back(entry.tag); }
        return tags;
    }

    int slowest_tag() const { return slowest(1).front(); }

    int fastest_tag() const {
        const std::vector<int> ordered = slowest(calls.size());
        return ordered.back();
    }
};

}  // namespace

TEST(the_capacity_defaults_to_the_macro) {
    // Which is 10 unless the build overrode it -- the matrix compiles this file
    // at 0 and 1 as well, so nothing below may assume the shipped default.
    CHECK_EQ(TEMPO_CALLABLE_METRICS(cheap)::worst_capacity,
             static_cast<std::size_t>(TEMPO_WORST_CALLS));
    CHECK_EQ(TEMPO_CALLABLE_METRICS(cheap)::ranks_worst, TEMPO_WORST_CALLS > 0);
}

TEST(the_capacity_is_whatever_the_caller_asks_for) {
    CHECK_EQ((tempo::CallableMetrics<&cheap, 1>::worst_capacity), 1u);
    CHECK_EQ((tempo::CallableMetrics<&cheap, 3>::worst_capacity), 3u);
    CHECK_EQ((tempo::CallableMetrics<&cheap, 64>::worst_capacity), 64u);
    CHECK_EQ((tempo::CallableMetrics<&cheap, 1000>::worst_capacity), 1000u);

    // Zero is legal and stores nothing; worst_calls() is then a compile error,
    // which tests/errors/15 pins.
    CHECK_EQ((tempo::CallableMetrics<&cheap, 0>::worst_capacity), 0u);
    CHECK(!(tempo::CallableMetrics<&cheap, 0>::ranks_worst));
}

TEST(a_different_capacity_is_a_different_metric) {
    // The capacity is part of the type, so it selects a different set of
    // statics -- the same rule that already applies to the wrapped callable.
    CHECK(!(std::is_same_v<tempo::CallableMetrics<&cheap, 4>,
                           tempo::CallableMetrics<&cheap, 5>>));
    CHECK((std::is_same_v<tempo::CallableMetrics<&cheap>,
                          tempo::CallableMetrics<&cheap, TEMPO_WORST_CALLS>>));
}

TEST(nothing_is_ranked_before_the_first_call) {
    using Metrics = tempo::CallableMetrics<&cheap, 4>;
    Metrics::reset();

    CHECK_EQ(Metrics::worst_calls().size(), 0u);
    CHECK_EQ(Metrics::snapshot().worst_calls().size(), 0u);
}

TEST(the_ranking_grows_until_it_is_full_then_stops) {
    using Metrics = tempo::CallableMetrics<&cheap, 3>;
    Metrics::reset();
    Metrics metrics;

    metrics(1, 1);
    CHECK_EQ(Metrics::worst_calls().size(), 1u);
    metrics(2, 2);
    CHECK_EQ(Metrics::worst_calls().size(), 2u);
    metrics(3, 3);
    CHECK_EQ(Metrics::worst_calls().size(), 3u);

    for (int i = 0; i < 50; ++i) { metrics(i, i); }
    CHECK_EQ(Metrics::worst_calls().size(), 3u);
    CHECK_EQ(Metrics::snapshot().calls, 53u);
}

TEST(entries_come_back_slowest_first) {
    using Metrics = tempo::CallableMetrics<&busy_ms, 3>;
    Metrics::reset();
    Metrics metrics;

    // Ordinarily this ranks 40, 20, 10 -- the three longest requests.
    Observer observer;
    observer.call(metrics, 1, 2);
    observer.call(metrics, 40, 40);
    observer.call(metrics, 10, 10);
    observer.call(metrics, 20, 20);
    observer.call(metrics, 3, 5);

    const auto ranked = Metrics::worst_calls();
    CHECK_EQ(ranked.size(), 3u);
    CHECK_EQ(ranked_tags<Metrics>(), observer.slowest(3));

    // Sorted descending, and consistent with the durations recorded alongside.
    CHECK_GE(ranked[0].duration.count(), ranked[1].duration.count());
    CHECK_GE(ranked[1].duration.count(), ranked[2].duration.count());
}

TEST(the_head_of_the_ranking_is_the_slowest_call) {
    // slowest_args() reads the ranking's head when there is one, so the two
    // must never disagree.
    using Metrics = tempo::CallableMetrics<&busy_ms, 5>;
    Metrics::reset();
    Metrics metrics;

    Observer observer;
    observer.call(metrics, 3, 5);
    observer.call(metrics, 40, 40);
    observer.call(metrics, 10, 15);

    const auto state = Metrics::snapshot();
    CHECK_EQ(std::get<1>(state.max_args), observer.slowest_tag());
    CHECK_EQ(std::get<1>(metrics.slowest_args()), observer.slowest_tag());
    CHECK_EQ(std::get<1>(state.worst_calls()[0].args), observer.slowest_tag());
    CHECK_EQ(state.worst_calls()[0].duration.count(), state.max_duration.count());

    // The fastest call is not the ranking's business and is tracked separately.
    CHECK_EQ(std::get<1>(state.min_args), observer.fastest_tag());
}

TEST(a_capacity_of_one_still_agrees_with_slowest_args) {
    using Metrics = tempo::CallableMetrics<&busy_ms, 1>;
    Metrics::reset();
    Metrics metrics;

    Observer observer;
    observer.call(metrics, 3, 5);
    observer.call(metrics, 40, 40);
    observer.call(metrics, 10, 15);

    CHECK_EQ(Metrics::worst_calls().size(), 1u);
    CHECK_EQ(std::get<1>(metrics.slowest_args()), observer.slowest_tag());
    CHECK_EQ(std::get<1>(Metrics::worst_calls()[0].args), observer.slowest_tag());
}

TEST(a_capacity_of_zero_still_tracks_the_single_slowest) {
    // Turning the ranking off must not cost the behaviour that predates it.
    using Metrics = tempo::CallableMetrics<&busy_ms, 0>;
    Metrics::reset();
    Metrics metrics;

    Observer observer;
    observer.call(metrics, 3, 5);
    observer.call(metrics, 40, 40);
    observer.call(metrics, 10, 15);

    CHECK_EQ(std::get<1>(metrics.slowest_args()), observer.slowest_tag());
    CHECK_EQ(std::get<1>(metrics.fastest_args()), observer.fastest_tag());
    CHECK_EQ(Metrics::snapshot().calls, 3u);
    CHECK_EQ(Metrics::snapshot().worst_calls().size(), 0u);
}

TEST(a_slower_call_displaces_the_tail_and_keeps_the_order) {
    using Metrics = tempo::CallableMetrics<&busy_ms, 3>;
    Metrics::reset();
    Metrics metrics;

    // Every step re-derives the expected top three from all the calls made so
    // far, so each displacement is checked against what actually happened.
    Observer observer;
    observer.call(metrics, 40, 40);
    observer.call(metrics, 20, 20);
    observer.call(metrics, 10, 10);
    CHECK_EQ(ranked_tags<Metrics>(), observer.slowest(3));

    // Lands in the middle: 10 is pushed out, 30 sits between 40 and 20.
    observer.call(metrics, 30, 30);
    CHECK_EQ(ranked_tags<Metrics>(), observer.slowest(3));

    // Faster than every entry: rejected, nothing moves.
    observer.call(metrics, 1, 2);
    CHECK_EQ(ranked_tags<Metrics>(), observer.slowest(3));

    // Slower than every entry: becomes the new head.
    observer.call(metrics, 80, 80);
    CHECK_EQ(ranked_tags<Metrics>(), observer.slowest(3));
    CHECK_EQ(std::get<1>(metrics.slowest_args()), observer.slowest_tag());
}

TEST(each_entry_carries_the_call_site_that_produced_it) {
    using Metrics = tempo::CallableMetrics<&busy_ms, 2>;
    Metrics::reset();
    Metrics metrics;

    // Timed in place rather than through Observer: the location tempo records
    // is the line of the call expression, so routing these through a helper
    // would make every entry point at the helper instead of at this test.
    const auto before = std::chrono::steady_clock::now();
    metrics(30, 30); const auto slow_line = __LINE__;
    const auto between = std::chrono::steady_clock::now();
    metrics(1, 1);    const auto fast_line = __LINE__;
    const auto after = std::chrono::steady_clock::now();

    // Normally the 30 ms call is the slower of the two; on a machine that
    // stalled the 1 ms one it is not, and the head of the ranking should follow
    // the durations rather than the request.
    // Cast because source_location::line() is unsigned and __LINE__ is int; the
    // two only compared cleanly before because the ternary here is not a
    // constant expression the way a bare __LINE__ is.
    const bool longer_call_was_slower = (between - before) >= (after - between);
    const auto head_line =
        static_cast<std::uint_least32_t>(longer_call_was_slower ? slow_line : fast_line);
    const auto tail_line =
        static_cast<std::uint_least32_t>(longer_call_was_slower ? fast_line : slow_line);

    const auto ranked = Metrics::worst_calls();
    CHECK_EQ(ranked.size(), 2u);
    CHECK_EQ(ranked[0].location.line(), head_line);
    CHECK_EQ(ranked[1].location.line(), tail_line);
    CHECK_NE(ranked[0].location.line(), ranked[1].location.line());
}

TEST(reset_clears_the_ranking) {
    using Metrics = tempo::CallableMetrics<&busy_ms, 3>;
    Metrics::reset();
    Metrics metrics;

    metrics(20, 20);
    metrics(1, 1);
    CHECK_EQ(Metrics::worst_calls().size(), 2u);

    Metrics::reset();
    CHECK_EQ(Metrics::worst_calls().size(), 0u);
    CHECK_EQ(Metrics::snapshot().worst_calls().size(), 0u);

    // And it refills from scratch rather than resuming the old ordering.
    metrics(1, 5);
    CHECK_EQ(Metrics::worst_calls().size(), 1u);
    CHECK_EQ(std::get<1>(Metrics::worst_calls()[0].args), 5);
}

TEST(a_snapshot_carries_the_ranking_it_was_taken_with) {
    using Metrics = tempo::CallableMetrics<&busy_ms, 3>;
    Metrics::reset();
    Metrics metrics;

    Observer observer;
    observer.call(metrics, 20, 20);
    const auto early = Metrics::snapshot();

    observer.call(metrics, 40, 40);
    const auto later = Metrics::snapshot();

    // The earlier snapshot is a value, not a view, so it did not change under us.
    CHECK_EQ(early.worst_calls().size(), 1u);
    CHECK_EQ(std::get<1>(early.worst_calls()[0].args), 20);
    CHECK_EQ(later.worst_calls().size(), 2u);
    CHECK_EQ(std::get<1>(later.worst_calls()[0].args), observer.slowest_tag());
}

// ---------------------------------------------------------------- arity

TEST(a_callable_with_no_parameters_at_all_still_ranks) {
    // StoredArgsType is std::tuple<>, so the entries carry only durations and
    // call sites. Nothing about the ranking may depend on there being an arg.
    using Metrics = tempo::CallableMetrics<&nullary, 4>;
    Metrics::reset();
    Metrics metrics;

    metrics();
    metrics();
    metrics();

    const auto ranked = Metrics::worst_calls();
    CHECK_EQ(ranked.size(), 3u);
    CHECK_EQ(std::tuple_size_v<decltype(ranked[0].args)>, 0u);
    CHECK_GE(ranked[0].duration.count(), ranked[2].duration.count());
    CHECK_GT(ranked[0].location.line(), 0u);
}

TEST(sixteen_parameters_are_stored_whole) {
    using Metrics = tempo::CallableMetrics<&wide, 2>;
    Metrics::reset();
    Metrics metrics;

    metrics(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);

    const auto ranked = Metrics::worst_calls();
    CHECK_EQ(ranked.size(), 1u);
    CHECK_EQ(std::tuple_size_v<decltype(ranked[0].args)>, 16u);
    CHECK_EQ(std::get<0>(ranked[0].args), 1);
    CHECK_EQ(std::get<15>(ranked[0].args), 16);
}

TEST(a_method_ranks_its_own_parameters_without_the_instance) {
    using Metrics = tempo::CallableMetrics<&Service::handle, 3>;
    Metrics::reset();
    Metrics metrics;
    Service service;

    // The instance comes first, so these go through Observer's general form.
    Observer observer;
    observer.call(30, [&] { metrics(service, 30, 30); });
    observer.call(1, [&] { metrics(&service, 1, 1); });

    const auto ranked = Metrics::worst_calls();
    CHECK_EQ(ranked.size(), 2u);
    CHECK_EQ(std::tuple_size_v<decltype(ranked[0].args)>, 2u);   // not 3
    CHECK_EQ(ranked_tags<Metrics>(), observer.slowest(2));
}

TEST(a_callable_whose_arguments_cannot_be_captured_still_ranks_durations) {
    // tracks_args is false for a move-only parameter, so args is the empty
    // tuple -- but the durations and call sites are still worth having.
    using Metrics = tempo::CallableMetrics<&consume, 3>;
    Metrics::reset();
    Metrics metrics;

    CHECK(!Metrics::tracks_args);
    CHECK(Metrics::ranks_worst);

    metrics(std::make_unique<int>(1));
    metrics(std::make_unique<int>(2));

    const auto ranked = Metrics::worst_calls();
    CHECK_EQ(ranked.size(), 2u);
    CHECK_EQ(std::tuple_size_v<decltype(ranked[0].args)>, 0u);
    CHECK_GE(ranked[0].duration.count(), ranked[1].duration.count());
}

TEST(a_noexcept_callable_ranks_without_weakening_its_guarantee) {
    struct Local { static int scale(int value) noexcept { return value * 2; } };
    using Metrics = tempo::CallableMetrics<&Local::scale, 4>;
    Metrics::reset();
    Metrics metrics;

    static_assert(Metrics::is_noexcept);
    static_assert(noexcept(metrics(1)));

    metrics(1);
    metrics(2);

    CHECK_EQ(Metrics::worst_calls().size(), 2u);
    CHECK_EQ(Metrics::snapshot().calls, 2u);
}

TEST(a_measured_lambda_takes_a_capacity_too) {
    auto parse = tempo::measure<2>([](int length) { return length; });

    parse(1);
    parse(2);
    parse(3);

    CHECK_EQ(decltype(parse)::worst_capacity, 2u);
    CHECK_EQ(decltype(parse)::worst_calls().size(), 2u);
    CHECK_EQ(decltype(parse)::snapshot().calls, 3u);

    auto plain = tempo::measure([](int length) { return length; });
    CHECK_EQ(decltype(plain)::worst_capacity, static_cast<std::size_t>(TEMPO_WORST_CALLS));
}

TEST(a_throwing_call_is_not_ranked) {
    struct Local {
        static int maybe(int value) {
            if (value < 0) { throw std::runtime_error{"negative"}; }
            return value;
        }
    };
    using Metrics = tempo::CallableMetrics<&Local::maybe, 4>;
    Metrics::reset();
    Metrics metrics;

    metrics(1);
    CHECK_THROWS_AS(metrics(-1), std::runtime_error);
    metrics(2);

    // Counted as calls, but a call that never finished has no duration to rank.
    CHECK_EQ(Metrics::snapshot().calls, 3u);
    CHECK_EQ(Metrics::worst_calls().size(), 2u);
    for (const auto& entry : Metrics::worst_calls()) {
        CHECK_GE(std::get<0>(entry.args), 0);
    }
}

TEST(entries_are_exact_across_threads) {
    using Metrics = tempo::CallableMetrics<&cheap, 8>;
    Metrics::reset();
    Metrics metrics;

    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&metrics, worker] {
            for (int i = 0; i < 250; ++i) { metrics(worker, i); }
        });
    }
    for (auto& worker : workers) { worker.join(); }

    const auto state = Metrics::snapshot();
    CHECK_EQ(state.calls, 1000u);
    CHECK_EQ(state.worst_calls().size(), 8u);

    // Still sorted, and still consistent with the maximum, under contention.
    const auto ranked = state.worst_calls();
    for (std::size_t i = 1; i < ranked.size(); ++i) {
        CHECK_GE(ranked[i - 1].duration.count(), ranked[i].duration.count());
    }
    CHECK_EQ(ranked[0].duration.count(), state.max_duration.count());
}
