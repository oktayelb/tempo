// 10 — Abuse
//
// Deliberately hostile usage: degenerate signatures, enormous arity, huge
// arguments, wrappers nested inside wrappers, reset called in the middle of
// everything, and types that break the usual assumptions. Nothing here is how
// tempo is meant to be used; the point is that none of it should misbehave.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------- degenerate signatures ----------
void takes_nothing_returns_nothing() {}
int returns_without_arguments() { return 99; }

// A parameter list long enough to stress the tuple machinery.
int sixteen_arguments(int a1, int a2, int a3, int a4, int a5, int a6, int a7,
                      int a8, int a9, int a10, int a11, int a12, int a13,
                      int a14, int a15, int a16) {
    return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 +
           a14 + a15 + a16;
}

// A large by-value parameter, so the stored snapshot is genuinely expensive.
struct Bulky {
    std::array<std::uint64_t, 256> payload{};
    int tag = 0;
    Bulky() = default;
    explicit Bulky(int t) : tag(t) { payload.fill(static_cast<std::uint64_t>(t)); }
};

int consume_bulky(Bulky value) { return value.tag; }

// Pointer, array-decayed and const-qualified parameters.
int count_chars(const char* text) {
    int n = 0;
    while (text && text[n] != '\0') { ++n; }
    return n;
}

int sum_span(const int* values, std::size_t count) {
    int total = 0;
    for (std::size_t i = 0; i < count; ++i) { total += values[i]; }
    return total;
}

// A type whose equality and default construction are unusual.
struct NoDefault {
    int value;
    explicit NoDefault(int v) : value(v) {}
};
int use_no_default(const NoDefault& n) { return n.value; }

// Deep call chains through several tempo layers, and the name-lookup subtlety
// that decides whether a nested call is seen at all.
namespace impl {
int inner(int x) { return x * 2; }
}
TEMPO_INSTRUMENT(impl::inner, inner);

namespace impl {
// Unqualified: inside namespace impl this finds impl::inner, the REAL function,
// so the wrapper never sees it. Same rule that keeps recursion uncounted.
int calls_inner_directly(int x) { return inner(x) + 1; }

// Qualified with ::, so this reaches the wrapper in the global scope.
int calls_inner_through_wrapper(int x) { return ::inner(x) + 1; }
}
TEMPO_INSTRUMENT(impl::calls_inner_directly, calls_inner_directly);
TEMPO_INSTRUMENT(impl::calls_inner_through_wrapper, calls_inner_through_wrapper);

struct Big {
    std::array<char, 1024> buffer{};
    int identify() const { return static_cast<int>(buffer.size()); }
};

}  // namespace

// Deliberately short names in a NAMED namespace, for the report test below.
// Anonymous-namespace names differ across compilers -- GCC prints
// "{anonymous}::f" while clang prints "&(anonymous namespace)::f" -- and the
// longer clang spelling can cross the report's 60 character column cap and be
// truncated. Short names keep that test about registration, not about
// formatting.
namespace named {
int alpha() { return 1; }
int beta(int x) { return x; }
int gamma(int x, int y) { return x + y; }
}  // namespace named

TEST(zero_arguments_and_void_return) {
    using Metrics = TEMPO_CALLABLE_METRICS(takes_nothing_returns_nothing);
    Metrics::reset();
    Metrics metrics;

    metrics();
    metrics();

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 2u);
    CHECK_EQ(stats.timed_calls, 2u);
    CHECK_EQ(std::tuple_size_v<Metrics::StoredArgsType>, 0u);
    CHECK(stats.has_samples);
    CHECK_GE(stats.total_duration.count(), 0.0);
}

TEST(no_arguments_with_a_return_value) {
    using Metrics = TEMPO_CALLABLE_METRICS(returns_without_arguments);
    Metrics::reset();
    Metrics metrics;

    CHECK_EQ(metrics(), 99);
    CHECK_EQ(Metrics::snapshot().calls, 1u);
}

TEST(sixteen_parameters_are_captured_correctly) {
    using Metrics = TEMPO_CALLABLE_METRICS(sixteen_arguments);
    Metrics::reset();
    Metrics metrics;

    CHECK_EQ(metrics(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16), 136);
    // Metrics forwards ReturnType and ArgsType but not arg_count, so the arity
    // is reached through the wrapper type.
    CHECK_EQ(Metrics::CallableType::arg_count, 16u);
    CHECK_EQ(std::tuple_size_v<Metrics::StoredArgsType>, 16u);

    const auto stats = Metrics::snapshot();
    CHECK_EQ(std::get<0>(stats.max_args), 1);
    CHECK_EQ(std::get<15>(stats.max_args), 16);
}

TEST(a_very_large_by_value_argument_survives_the_round_trip) {
    using Metrics = TEMPO_CALLABLE_METRICS(consume_bulky);
    Metrics::reset();
    Metrics metrics;

    CHECK_EQ(metrics(Bulky{7}), 7);
    CHECK_EQ(Metrics::snapshot().calls, 1u);
    CHECK_EQ(std::get<0>(Metrics::snapshot().max_args).tag, 7);
    CHECK_EQ(std::get<0>(Metrics::snapshot().max_args).payload[255], 7u);
}

TEST(pointer_and_array_parameters_work) {
    using Metrics = TEMPO_CALLABLE_METRICS(count_chars);
    Metrics::reset();
    Metrics metrics;

    CHECK_EQ(metrics("hello"), 5);
    CHECK_EQ(metrics(nullptr), 0);      // a null pointer must not crash tempo
    CHECK_EQ(Metrics::snapshot().calls, 2u);

    using SpanMetrics = TEMPO_CALLABLE_METRICS(sum_span);
    SpanMetrics::reset();
    SpanMetrics span_metrics;

    const int values[] = {1, 2, 3, 4};
    CHECK_EQ(span_metrics(values, 4u), 10);
    CHECK_EQ(span_metrics(nullptr, 0u), 0);
}

TEST(a_parameter_type_without_a_default_constructor_disables_storage) {
    using Metrics = TEMPO_CALLABLE_METRICS(use_no_default);
    static_assert(!Metrics::tracks_args);
    static_assert(std::is_same_v<Metrics::StoredArgsType, std::tuple<>>);

    Metrics::reset();
    Metrics metrics;

    // Timing still works; only argument capture switches itself off.
    CHECK_EQ(metrics(NoDefault{5}), 5);
    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 1u);
    CHECK(stats.has_samples);
    CHECK_GE(stats.total_duration.count(), 0.0);
}

TEST(an_unqualified_nested_call_bypasses_the_wrapper) {
    decltype(inner)::reset();
    decltype(calls_inner_directly)::reset();

    CHECK_EQ(calls_inner_directly(5), 11);

    // The outer call is measured, the inner one is invisible: inside namespace
    // impl the name `inner` finds impl::inner, not the global wrapper. This is
    // the same lookup rule that keeps recursion uncounted by default, and it is
    // why TEMPO_SELF has to exist.
    CHECK_EQ(decltype(calls_inner_directly)::snapshot().calls, 1u);
    CHECK_EQ(decltype(inner)::snapshot().calls, 0u);
}

TEST(a_qualified_nested_call_is_seen_by_both_wrappers) {
    decltype(inner)::reset();
    decltype(calls_inner_through_wrapper)::reset();

    CHECK_EQ(calls_inner_through_wrapper(5), 11);

    // Each keeps its own books. The inner call is nested inside the outer one,
    // but they are different types with separate depth counters, so neither
    // gate affects the other and both record a normal top-level call.
    CHECK_EQ(decltype(calls_inner_through_wrapper)::snapshot().calls, 1u);
    CHECK_EQ(decltype(inner)::snapshot().calls, 1u);
    CHECK_EQ(decltype(calls_inner_through_wrapper)::snapshot().max_depth, 1u);
    CHECK_EQ(decltype(inner)::snapshot().max_depth, 1u);
    CHECK_EQ(decltype(inner)::snapshot().timed_calls, 1u);

    // The outer duration contains the inner one: ordinary inclusive timing
    // between two different functions, which is correct and not double counting.
    const auto outer_stats = decltype(calls_inner_through_wrapper)::snapshot();
    const auto inner_stats = decltype(inner)::snapshot();
    CHECK_GE(outer_stats.total_duration.count(), 0.0);
    CHECK_GE(inner_stats.total_duration.count(), 0.0);
}

TEST(metrics_wrapping_a_lambda_that_calls_another_metrics) {
    auto innermost = tempo::measure([](int x) { return x + 1; });
    decltype(innermost)::reset();

    auto outermost = tempo::measure([&innermost](int x) { return innermost(x) * 2; });
    decltype(outermost)::reset();

    CHECK_EQ(outermost(3), 8);
    CHECK_EQ(decltype(innermost)::snapshot().calls, 1u);
    CHECK_EQ(decltype(outermost)::snapshot().calls, 1u);
}

TEST(reset_in_the_middle_of_a_run_does_not_corrupt_anything) {
    using Metrics = TEMPO_CALLABLE_METRICS(returns_without_arguments);
    Metrics::reset();
    Metrics metrics;

    for (int i = 0; i < 100; ++i) {
        metrics();
        if (i == 50) { Metrics::reset(); }
    }

    // 100 calls, reset once after the 51st, so 49 remain counted.
    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 49u);
    CHECK_EQ(stats.timed_calls, 49u);
    CHECK_LE(stats.min_duration.count(), stats.max_duration.count());
    CHECK_EQ(stats.max_depth, 1u);
}

TEST(repeated_resets_are_harmless) {
    using Metrics = TEMPO_CALLABLE_METRICS(returns_without_arguments);
    for (int i = 0; i < 100; ++i) { Metrics::reset(); }

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, 0u);
    CHECK(!stats.has_samples);
    CHECK_EQ(stats.average_ms(), 0.0);
}

TEST(a_very_long_run_does_not_drift) {
    using Metrics = TEMPO_CALLABLE_METRICS(returns_without_arguments);
    Metrics::reset();
    Metrics metrics;

    // Far fewer when per-call printing is compiled in: this test is about the
    // counters not drifting, and 200k calls times nine lines of output each is
    // two million lines of noise that proves nothing extra.
#if TEMPO_PRINT_ENABLED
    constexpr unsigned iterations = 200;
#else
    constexpr unsigned iterations = 200000;
#endif
    for (unsigned i = 0; i < iterations; ++i) { metrics(); }

    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, iterations);
    CHECK_EQ(stats.timed_calls, iterations);
    CHECK_LE(stats.min_duration.count(), stats.average_ms());
    CHECK_LE(stats.average_ms(), stats.max_duration.count());
    CHECK_GT(stats.total_duration.count(), 0.0);
}

TEST(a_large_object_as_the_member_instance) {
    using Metrics = TEMPO_CALLABLE_METRICS(Big::identify);
    Metrics::reset();
    Metrics metrics;

    Big big;
    CHECK_EQ(metrics(big), 1024);
    CHECK_EQ(metrics(&big), 1024);
    CHECK_EQ(Metrics::snapshot().calls, 2u);
    // The instance is not stored, however big it is.
    CHECK_EQ(std::tuple_size_v<Metrics::StoredArgsType>, 0u);
}

TEST(many_distinct_instantiations_all_register_for_reporting) {
    tempo::reset_all();

    using A = TEMPO_CALLABLE_METRICS(named::alpha);
    using B = TEMPO_CALLABLE_METRICS(named::beta);
    using C = TEMPO_CALLABLE_METRICS(named::gamma);

    A a;
    B b;
    C c;
    a();
    b(1);
    c(1, 2);

    std::ostringstream out;
    tempo::report(out);
    const std::string text = out.str();

    // Each instantiation registers itself on its first call, so all three must
    // appear as separate rows.
    CHECK(text.find("alpha") != std::string::npos);
    CHECK(text.find("beta") != std::string::npos);
    CHECK(text.find("gamma") != std::string::npos);
}

TEST(an_uncalled_metric_never_appears_in_the_report) {
    tempo::reset_all();

    using Called = TEMPO_CALLABLE_METRICS(named::alpha);
    using NeverCalled = TEMPO_CALLABLE_METRICS(named::gamma);

    Called called;
    NeverCalled never_called;
    called();
    (void)never_called;

    std::ostringstream out;
    tempo::report(out);
    const std::string text = out.str();

    CHECK(text.find("alpha") != std::string::npos);
    // Registration happens on first call, and rows with zero calls are dropped.
    CHECK(text.find("gamma") == std::string::npos);
}

TEST(report_truncates_absurdly_long_type_names_without_breaking) {
    tempo::reset_all();

    // std::function's type name is long; the table caps the column at 60 and
    // must not corrupt its own layout doing so.
    using Wrapped = tempo::Metrics<tempo::Functor<std::function<int(int, int, int)>>>;
    Wrapped::reset();
    Wrapped wrapped{{}, tempo::Functor<std::function<int(int, int, int)>>{
                            std::function<int(int, int, int)>{
                                [](int a, int b, int c) { return a + b + c; }}}};
    wrapped(1, 2, 3);

    std::ostringstream out;
    tempo::report(out);
    const std::string text = out.str();

    CHECK(text.find("tempo report") != std::string::npos);
    // Every line stays within a sane width rather than running away.
    std::istringstream lines{text};
    std::string line;
    while (std::getline(lines, line)) { CHECK_LT(line.size(), 200u); }
}

TEST(calling_through_std_function_wrapping_a_wrapper) {
    using Metrics = TEMPO_CALLABLE_METRICS(returns_without_arguments);
    Metrics::reset();
    Metrics metrics;

    // Erase the wrapper behind std::function and call it that way.
    std::function<int()> erased = [&metrics] { return metrics(); };
    CHECK_EQ(erased(), 99);
    CHECK_EQ(erased(), 99);
    CHECK_EQ(Metrics::snapshot().calls, 2u);
}

TEST(const_wrappers_are_callable) {
    // operator() is const, so a const wrapper object must still work.
    using Metrics = TEMPO_CALLABLE_METRICS(returns_without_arguments);
    Metrics::reset();
    const Metrics metrics;

    CHECK_EQ(metrics(), 99);
    CHECK_EQ(Metrics::snapshot().calls, 1u);
}

TEST(copying_a_wrapper_object_shares_the_statistics) {
    using Metrics = TEMPO_CALLABLE_METRICS(returns_without_arguments);
    Metrics::reset();

    Metrics original;
    original();

    Metrics copy = original;   // statistics are static, so they are shared
    copy();

    CHECK_EQ(Metrics::snapshot().calls, 2u);
}
