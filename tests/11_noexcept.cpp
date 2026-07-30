// noexcept callables.
//
// The promise being tested is that wrapping a noexcept callable does not change
// the exception specification its callers see. TEMPO_INSTRUMENT works by giving
// the wrapper the function's own name, so the wrapper's type IS the type every
// call site now resolves against -- if the noexcept were dropped there, code
// that relied on it (a noexcept move constructor, a container's strong
// guarantee, another function's noexcept(f(x)) specification) would silently
// change meaning with no diagnostic anywhere.
//
// The second half is the price of that promise: capturing an argument means
// copying it, and a copy that can throw cannot happen inside a wrapper that has
// promised not to. Those callables keep their timings and lose only argument
// capture, which tracks_args reports at compile time.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace impl {

int scale(int value) noexcept { return value * 2; }
int loose(int value) { return value * 3; }
void nothing() noexcept {}
int no_args() noexcept { return 7; }
std::size_t width(std::string text) noexcept { return text.size(); }
int by_ref(const int& value) noexcept { return value; }

} // namespace impl

struct Service {
    int calls = 0;
    int handle(int value) noexcept { ++calls; return value + 1; }
    int peek(int value) const noexcept { return value - 1; }
    int loose(int value) { return value; }
    int strict(int value) const { return value; }
};

// A type whose copy is not noexcept, but which is otherwise perfectly storable.
// This is the whole point of the stricter rule: nothing about it is move-only or
// unconstructible, it is simply a copy that could throw.
struct ThrowingCopy {
    int value = 0;
    ThrowingCopy() = default;
    ThrowingCopy(int v) : value(v) {}
    ThrowingCopy(const ThrowingCopy& other) noexcept(false) : value(other.value) {}
    ThrowingCopy& operator=(const ThrowingCopy&) noexcept(false) { return *this; }
};

namespace impl { int takes_throwing_copy(ThrowingCopy arg) noexcept { return arg.value; } }

TEMPO_INSTRUMENT(impl::scale, scale);
TEMPO_INSTRUMENT(impl::loose, loose);
TEMPO_INSTRUMENT(impl::no_args, no_args);
TEMPO_INSTRUMENT(impl::width, width);

// --- the trait, on every shape -------------------------------------------

TEST(is_noexcept_is_read_off_a_free_function) {
    static_assert(tempo::Callable<&impl::scale>::is_noexcept);
    static_assert(!tempo::Callable<&impl::loose>::is_noexcept);
    static_assert(tempo::Callable<&impl::nothing>::is_noexcept);
    CHECK(true);
}

TEST(is_noexcept_is_read_off_a_method_independently_of_const) {
    static_assert(tempo::Callable<&Service::handle>::is_noexcept);
    static_assert(!tempo::Callable<&Service::handle>::is_const_member);

    static_assert(tempo::Callable<&Service::peek>::is_noexcept);
    static_assert(tempo::Callable<&Service::peek>::is_const_member);

    static_assert(!tempo::Callable<&Service::loose>::is_noexcept);
    static_assert(!tempo::Callable<&Service::loose>::is_const_member);

    static_assert(!tempo::Callable<&Service::strict>::is_noexcept);
    static_assert(tempo::Callable<&Service::strict>::is_const_member);
    CHECK(true);
}

TEST(the_rest_of_the_signature_survives_the_noexcept) {
    using Scale = tempo::Callable<&impl::scale>;
    static_assert(std::is_same_v<Scale::ReturnType, int>);
    static_assert(std::is_same_v<Scale::ArgsType, std::tuple<int>>);
    static_assert(Scale::arg_count == 1);
    static_assert(!Scale::is_member);

    using Handle = tempo::Callable<&Service::handle>;
    static_assert(std::is_same_v<Handle::ClassType, Service>);
    static_assert(Handle::is_member);
    static_assert(Handle::arg_count == 1);
    CHECK(true);
}

// --- the guarantee actually reaching the caller ---------------------------

TEST(an_instrumented_noexcept_function_is_still_noexcept_at_the_call_site) {
    // The whole point. `scale` here is the wrapper, not impl::scale.
    static_assert(noexcept(scale(1)));
    static_assert(!noexcept(loose(1)));
    static_assert(noexcept(no_args()));
    CHECK_EQ(scale(21), 42);
    CHECK_EQ(loose(2), 6);
}

TEST(a_wrapped_method_is_noexcept_through_the_call_operator) {
    tempo::CallableMetrics<&Service::handle> metrics;
    tempo::CallableMetrics<&Service::loose> loose_metrics;
    Service service;

    static_assert(noexcept(metrics(service, 1)));
    static_assert(!noexcept(loose_metrics(service, 1)));

    CHECK_EQ(metrics(service, 41), 42);
    CHECK_EQ(service.calls, 1);
}

TEST(a_noexcept_lambda_gets_a_noexcept_wrapper) {
    auto strict = tempo::measure([](int value) noexcept { return value + 1; });
    auto relaxed = tempo::measure([](int value) { return value + 1; });

    static_assert(decltype(strict)::is_noexcept);
    static_assert(!decltype(relaxed)::is_noexcept);
    static_assert(noexcept(TEMPO_METRICS_CALL(strict, 1)));
    static_assert(!noexcept(TEMPO_METRICS_CALL(relaxed, 1)));

    CHECK_EQ(strict(1), 2);
    CHECK_EQ(relaxed(1), 2);
}

TEST(the_profiler_propagates_noexcept_too) {
    tempo::CallableProfiler<&impl::scale> strict;
    tempo::CallableProfiler<&impl::loose> relaxed;

    static_assert(decltype(strict)::is_noexcept);
    static_assert(!decltype(relaxed)::is_noexcept);
    static_assert(noexcept(strict(2)));
    static_assert(!noexcept(relaxed(2)));

    CHECK_EQ(strict(2), 4);
}

// --- measurement is unaffected -------------------------------------------

TEST(a_noexcept_callable_is_measured_like_any_other) {
    using Scale = decltype(scale);
    Scale::reset();

    scale(1);
    scale(2);
    scale(3);

    const auto stats = Scale::snapshot();
    CHECK_EQ(stats.calls, 3u);
    CHECK_EQ(stats.timed_calls, 3u);
    CHECK(stats.has_samples);
    CHECK(stats.total_duration.count() >= 0.0);
    CHECK(stats.min_duration <= stats.max_duration);
}

TEST(argument_capture_works_when_every_parameter_copies_without_throwing) {
    using Scale = decltype(scale);
    static_assert(Scale::tracks_args);

    Scale::reset();
    scale(1);
    scale(2);
    scale(3);

    // Every call is the same work, so which one came out slowest is not
    // predictable -- but it has to be one of the three that actually ran.
    const int worst = std::get<0>(scale.get_maximizers());
    const int best = std::get<0>(scale.get_minimizers());
    CHECK(worst >= 1 && worst <= 3);
    CHECK(best >= 1 && best <= 3);
}

TEST(a_reference_parameter_still_captures_since_the_stored_type_is_decayed) {
    using ByRef = tempo::CallableMetrics<&impl::by_ref>;
    static_assert(ByRef::is_noexcept);
    // const int& decays to int, whose copy is trivially noexcept.
    static_assert(ByRef::tracks_args);
    CHECK(true);
}

TEST(no_arguments_at_all_is_storable_under_noexcept) {
    using NoArgs = decltype(no_args);
    static_assert(NoArgs::is_noexcept);
    static_assert(NoArgs::tracks_args);

    NoArgs::reset();
    CHECK_EQ(no_args(), 7);
    CHECK_EQ(NoArgs::snapshot().calls, 1u);
}

// --- the price ------------------------------------------------------------

TEST(capture_switches_off_when_a_parameter_copy_could_throw) {
    using Width = decltype(width);
    static_assert(Width::is_noexcept);
    // std::string is copy-constructible and default-constructible, so the
    // ordinary rule would have stored it. Copying it allocates.
    static_assert(std::is_copy_constructible_v<std::string>);
    static_assert(!std::is_nothrow_copy_constructible_v<std::string>);
    static_assert(!Width::tracks_args);

    // Everything except capture keeps working.
    Width::reset();
    CHECK_EQ(width(std::string{"abcd"}), 4u);
    CHECK_EQ(Width::snapshot().calls, 1u);
    CHECK(Width::snapshot().has_samples);
}

TEST(the_same_parameter_still_captures_on_a_throwing_callable) {
    // The restriction is a consequence of the noexcept, not of std::string.
    // Without the noexcept the very same signature captures as it always did.
    static_assert(tempo::CallableMetrics<&impl::takes_throwing_copy>::is_noexcept);
    static_assert(!tempo::CallableMetrics<&impl::takes_throwing_copy>::tracks_args);

    auto relaxed = tempo::measure([](std::string text) { return text.size(); });
    static_assert(!decltype(relaxed)::is_noexcept);
    static_assert(decltype(relaxed)::tracks_args);

    CHECK_EQ(TEMPO_METRICS_CALL(relaxed, std::string{"hello"}), 5u);
    CHECK_EQ(std::get<0>(relaxed.get_maximizers()), std::string{"hello"});
}

TEST(a_move_only_parameter_is_still_off_for_the_reason_it_always_was) {
    auto consume = tempo::measure(
        [](std::unique_ptr<int> owned) noexcept { return *owned; });
    static_assert(decltype(consume)::is_noexcept);
    static_assert(!decltype(consume)::tracks_args);
    CHECK_EQ(TEMPO_METRICS_CALL(consume, std::make_unique<int>(99)), 99);
}

// --- interaction with the rest of the header ------------------------------

TEST(a_noexcept_callable_appears_in_the_report_like_any_other) {
    using Scale = decltype(scale);
    Scale::reset();
    scale(5);

    std::ostringstream out;
    tempo::report::print(out);
    const std::string text = out.str();
    CHECK(text.find("scale") != std::string::npos);
}

TEST(a_noexcept_call_site_is_still_captured_by_the_defaulted_source_location) {
    // The seam that makes a bare scale(1) record its caller is a defaulted
    // source_location parameter on a FIXED signature (call_operators::FixedSignatureCall).
    // That signature is where the noexcept had to be reapplied, so this checks
    // the two features still work together rather than one having displaced the
    // other.
    using Scale = decltype(scale);
    Scale::reset();

    const int line = __LINE__ + 1;
    scale(9);

    const auto stats = Scale::snapshot();
    CHECK_EQ(stats.last_call_location.line(), static_cast<unsigned>(line));
    CHECK(std::string_view{stats.last_call_location.file_name()}
              .find("11_noexcept") != std::string_view::npos);
}

TEST(the_wrapper_of_a_noexcept_callable_is_itself_a_noexcept_callable) {
    // Nesting: measuring a lambda that calls an instrumented noexcept function
    // must not need a try/catch anywhere, i.e. the inner call is still noexcept
    // as far as the outer lambda's deduced specification is concerned.
    auto outer = tempo::measure([](int value) noexcept { return scale(value); });
    static_assert(decltype(outer)::is_noexcept);
    CHECK_EQ(outer(4), 8);
}
