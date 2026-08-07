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

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

int sleep_ms(int milliseconds, int tag) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
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
    int handle(int milliseconds, int tag) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        return tag;
    }
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
    using Metrics = tempo::CallableMetrics<&sleep_ms, 3>;
    Metrics::reset();
    Metrics metrics;

    // Each sleep is at least double the next, so no plausible amount of noise
    // can reorder them.
    metrics(2, 2);
    metrics(40, 40);
    metrics(10, 10);
    metrics(20, 20);
    metrics(5, 5);

    const auto ranked = Metrics::worst_calls();
    CHECK_EQ(ranked.size(), 3u);
    CHECK_EQ(ranked_tags<Metrics>(), (std::vector<int>{40, 20, 10}));

    // Sorted descending, and consistent with the durations recorded alongside.
    CHECK_GE(ranked[0].duration.count(), ranked[1].duration.count());
    CHECK_GE(ranked[1].duration.count(), ranked[2].duration.count());
}

TEST(the_head_of_the_ranking_is_the_slowest_call) {
    // slowest_args() reads the ranking's head when there is one, so the two
    // must never disagree.
    using Metrics = tempo::CallableMetrics<&sleep_ms, 5>;
    Metrics::reset();
    Metrics metrics;

    metrics(5, 5);
    metrics(40, 40);
    metrics(15, 15);

    const auto state = Metrics::snapshot();
    CHECK_EQ(std::get<1>(state.max_args), 40);
    CHECK_EQ(std::get<1>(metrics.slowest_args()), 40);
    CHECK_EQ(std::get<1>(state.worst_calls()[0].args), 40);
    CHECK_EQ(state.worst_calls()[0].duration.count(), state.max_duration.count());

    // The fastest call is not the ranking's business and is tracked separately.
    CHECK_EQ(std::get<1>(state.min_args), 5);
}

TEST(a_capacity_of_one_still_agrees_with_slowest_args) {
    using Metrics = tempo::CallableMetrics<&sleep_ms, 1>;
    Metrics::reset();
    Metrics metrics;

    metrics(5, 5);
    metrics(40, 40);
    metrics(15, 15);

    CHECK_EQ(Metrics::worst_calls().size(), 1u);
    CHECK_EQ(std::get<1>(metrics.slowest_args()), 40);
    CHECK_EQ(std::get<1>(Metrics::worst_calls()[0].args), 40);
}

TEST(a_capacity_of_zero_still_tracks_the_single_slowest) {
    // Turning the ranking off must not cost the behaviour that predates it.
    using Metrics = tempo::CallableMetrics<&sleep_ms, 0>;
    Metrics::reset();
    Metrics metrics;

    metrics(5, 5);
    metrics(40, 40);
    metrics(15, 15);

    CHECK_EQ(std::get<1>(metrics.slowest_args()), 40);
    CHECK_EQ(std::get<1>(metrics.fastest_args()), 5);
    CHECK_EQ(Metrics::snapshot().calls, 3u);
    CHECK_EQ(Metrics::snapshot().worst_calls().size(), 0u);
}

TEST(a_slower_call_displaces_the_tail_and_keeps_the_order) {
    using Metrics = tempo::CallableMetrics<&sleep_ms, 3>;
    Metrics::reset();
    Metrics metrics;

    metrics(40, 40);
    metrics(20, 20);
    metrics(10, 10);
    CHECK_EQ(ranked_tags<Metrics>(), (std::vector<int>{40, 20, 10}));

    // Lands in the middle: 10 is pushed out, 30 sits between 40 and 20.
    metrics(30, 30);
    CHECK_EQ(ranked_tags<Metrics>(), (std::vector<int>{40, 30, 20}));

    // Faster than every entry: rejected, nothing moves.
    metrics(2, 2);
    CHECK_EQ(ranked_tags<Metrics>(), (std::vector<int>{40, 30, 20}));

    // Slower than every entry: becomes the new head.
    metrics(80, 80);
    CHECK_EQ(ranked_tags<Metrics>(), (std::vector<int>{80, 40, 30}));
    CHECK_EQ(std::get<1>(metrics.slowest_args()), 80);
}

TEST(each_entry_carries_the_call_site_that_produced_it) {
    using Metrics = tempo::CallableMetrics<&sleep_ms, 2>;
    Metrics::reset();
    Metrics metrics;

    metrics(30, 30);  const auto slow_line = __LINE__;
    metrics(1, 1);    const auto fast_line = __LINE__;

    const auto ranked = Metrics::worst_calls();
    CHECK_EQ(ranked.size(), 2u);
    CHECK_EQ(ranked[0].location.line(), slow_line);
    CHECK_EQ(ranked[1].location.line(), fast_line);
    CHECK_NE(ranked[0].location.line(), ranked[1].location.line());
}

TEST(reset_clears_the_ranking) {
    using Metrics = tempo::CallableMetrics<&sleep_ms, 3>;
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
    using Metrics = tempo::CallableMetrics<&sleep_ms, 3>;
    Metrics::reset();
    Metrics metrics;

    metrics(20, 20);
    const auto early = Metrics::snapshot();

    metrics(40, 40);
    const auto later = Metrics::snapshot();

    // The earlier snapshot is a value, not a view, so it did not change under us.
    CHECK_EQ(early.worst_calls().size(), 1u);
    CHECK_EQ(std::get<1>(early.worst_calls()[0].args), 20);
    CHECK_EQ(later.worst_calls().size(), 2u);
    CHECK_EQ(std::get<1>(later.worst_calls()[0].args), 40);
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

    metrics(service, 30, 30);
    metrics(&service, 1, 1);

    const auto ranked = Metrics::worst_calls();
    CHECK_EQ(ranked.size(), 2u);
    CHECK_EQ(std::tuple_size_v<decltype(ranked[0].args)>, 2u);   // not 3
    CHECK_EQ(std::get<1>(ranked[0].args), 30);
    CHECK_EQ(std::get<1>(ranked[1].args), 1);
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
