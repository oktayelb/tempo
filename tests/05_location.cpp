// 05 — Call-site capture
//
// The point of the fixed-signature operator() is that a bare call records the
// CALLER's file and line, with no macro. These tests compare against __LINE__
// taken at the call site, so they fail if the default argument ever starts being
// evaluated somewhere other than the caller.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <string>

namespace impl {
int add(int a, int b) { return a + b; }
int no_args() { return 7; }
int by_ref(const std::string& s) { return static_cast<int>(s.size()); }
}  // namespace impl

TEMPO_INSTRUMENT(impl::add, add);
TEMPO_INSTRUMENT(impl::no_args, no_args);
TEMPO_INSTRUMENT(impl::by_ref, by_ref);

namespace {

struct Service {
    int handle(int value) { return value; }
};

int plain(int x) { return x; }

}  // namespace

TEST(bare_call_records_the_caller_not_the_header) {
    decltype(add)::reset();

    const int expected = __LINE__ + 1;
    add(1, 2);

    const auto location = decltype(add)::snapshot().last_call_location;
    CHECK_EQ(location.line(), static_cast<unsigned>(expected));

    // The file must be this test, not tempo.hpp. Before the fixed signature,
    // source_location::current() fired inside the header.
    const std::string file = location.file_name();
    CHECK(file.find("05_location") != std::string::npos);
    CHECK(file.find("tempo.hpp") == std::string::npos);
}

TEST(every_call_updates_the_location) {
    decltype(add)::reset();

    const int first_line = __LINE__ + 1;
    add(1, 1);
    CHECK_EQ(decltype(add)::snapshot().last_call_location.line(),
             static_cast<unsigned>(first_line));

    const int second_line = __LINE__ + 1;
    add(2, 2);
    CHECK_EQ(decltype(add)::snapshot().last_call_location.line(),
             static_cast<unsigned>(second_line));

    CHECK_NE(first_line, second_line);
}

TEST(zero_argument_callables_still_capture_the_call_site) {
    decltype(no_args)::reset();

    const int expected = __LINE__ + 1;
    CHECK_EQ(no_args(), 7);

    CHECK_EQ(decltype(no_args)::snapshot().last_call_location.line(),
             static_cast<unsigned>(expected));
}

TEST(reference_parameters_do_not_disturb_capture) {
    decltype(by_ref)::reset();

    const int expected = __LINE__ + 1;
    CHECK_EQ(by_ref("hello"), 5);

    CHECK_EQ(decltype(by_ref)::snapshot().last_call_location.line(),
             static_cast<unsigned>(expected));
}

TEST(the_enclosing_function_name_is_recorded) {
    decltype(add)::reset();
    add(1, 1);

    const std::string function = decltype(add)::snapshot().last_call_location.function_name();
    CHECK(function.find("the_enclosing_function_name_is_recorded") != std::string::npos);
}

TEST(a_locally_declared_metric_also_records_the_call_site) {
    using Metrics = TEMPO_CALLABLE_METRICS(plain);
    Metrics::reset();
    Metrics metrics;

    const int expected = __LINE__ + 1;
    metrics(5);

    CHECK_EQ(Metrics::snapshot().last_call_location.line(),
             static_cast<unsigned>(expected));
}

TEST(member_functions_capture_the_call_site) {
    using Metrics = TEMPO_CALLABLE_METRICS(Service::handle);
    Metrics::reset();
    Metrics metrics;
    Service service;

    const int expected = __LINE__ + 1;
    metrics(service, 3);

    CHECK_EQ(Metrics::snapshot().last_call_location.line(),
             static_cast<unsigned>(expected));
}

TEST(members_accept_every_instance_form_and_still_capture_the_caller) {
    // The instance parameter is deduced, so every form std::invoke accepts binds
    // to it; the method's own parameters come from the class template, which
    // leaves the trailing source_location defaulting at the call site. A single
    // deduced parameter can do this -- a deduced pack could not, because it
    // would swallow the location argument.
    using Metrics = TEMPO_CALLABLE_METRICS(Service::handle);
    Metrics::reset();
    Metrics metrics;
    Service service;

    const int by_reference = __LINE__ + 1;
    CHECK_EQ(metrics(service, 1), 1);
    CHECK_EQ(Metrics::snapshot().last_call_location.line(),
             static_cast<unsigned>(by_reference));

    const int by_pointer = __LINE__ + 1;
    CHECK_EQ(metrics(&service, 2), 2);
    CHECK_EQ(Metrics::snapshot().last_call_location.line(),
             static_cast<unsigned>(by_pointer));

    const int by_reference_wrapper = __LINE__ + 1;
    CHECK_EQ(metrics(std::ref(service), 3), 3);
    CHECK_EQ(Metrics::snapshot().last_call_location.line(),
             static_cast<unsigned>(by_reference_wrapper));

    CHECK_EQ(Metrics::snapshot().calls, 3u);
}

TEST(profiler_records_its_own_last_location) {
    using Profiler = TEMPO_CALLABLE_PROFILER(plain);
    Profiler profiler;

    const int expected = __LINE__ + 1;
    TEMPO_PROFILE_CALL(profiler, 1);

    CHECK_EQ(Profiler::get_last_call_location().line(),
             static_cast<unsigned>(expected));
}

TEST(instrumented_names_forward_return_values_correctly) {
    decltype(add)::reset();
    CHECK_EQ(add(2, 3), 5);
    CHECK_EQ(add(-4, 4), 0);
    CHECK_EQ(decltype(add)::snapshot().calls, 2u);
}

TEST(instrumented_names_record_arguments) {
    decltype(add)::reset();
    add(1, 1);
    add(1000, 2000);

    const auto stats = decltype(add)::snapshot();
    CHECK_EQ(stats.calls, 2u);
    // Both calls are recorded somewhere between min and max; whichever was
    // slower, the stored pair must be one of the two we actually made.
    const auto max_first = std::get<0>(stats.max_args);
    CHECK((max_first == 1) || (max_first == 1000));
}
