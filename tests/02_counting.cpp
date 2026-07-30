// 02 — Call counting and reset
//
// Counters live on the wrapped TYPE, not on the wrapper object. That is a real
// design consequence with sharp edges, and these tests pin the edges down rather
// than pretending they are not there.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <functional>
#include <memory>
#include <string>

namespace {

int add(int a, int b) { return a + b; }
int multiply(int a, int b) { return a * b; }

struct Service {
    int calls_seen = 0;
    int handle(int value) { return ++calls_seen + value; }
    int inspect(int value) const { return value; }
};

}  // namespace

TEST(free_function_counts_every_call) {
    using Add = TEMPO_CALLABLE(add);
    Add::call_count = 0;

    Add wrapper;
    for (int i = 0; i < 10; ++i) { CHECK_EQ(wrapper(i, 1), i + 1); }
    CHECK_EQ(Add::call_count.load(), 10u);
}

TEST(counter_is_shared_by_every_instance_of_the_type) {
    using Add = TEMPO_CALLABLE(add);
    Add::call_count = 0;

    Add first;
    Add second;
    first(1, 1);
    second(2, 2);
    first(3, 3);

    // Three separate calls through two separate objects, one counter. This is
    // the documented consequence of the counter being static per type.
    CHECK_EQ(Add::call_count.load(), 3u);
}

TEST(different_functions_have_independent_counters) {
    using Add = TEMPO_CALLABLE(add);
    using Mul = TEMPO_CALLABLE(multiply);
    Add::call_count = 0;
    Mul::call_count = 0;

    Add a;
    Mul m;
    a(1, 1);
    a(2, 2);
    m(3, 3);

    CHECK_EQ(Add::call_count.load(), 2u);
    CHECK_EQ(Mul::call_count.load(), 1u);
}

TEST(member_and_const_member_count_separately) {
    using Handle = TEMPO_CALLABLE(Service::handle);
    using Inspect = TEMPO_CALLABLE(Service::inspect);
    Handle::call_count = 0;
    Inspect::call_count = 0;

    Service service;
    Handle handle;
    Inspect inspect;

    handle(service, 1);
    handle(service, 2);
    inspect(service, 3);

    CHECK_EQ(Handle::call_count.load(), 2u);
    CHECK_EQ(Inspect::call_count.load(), 1u);
    CHECK_EQ(service.calls_seen, 2);
}

TEST(member_accepts_every_instance_form_invoke_understands) {
    using Handle = TEMPO_CALLABLE(Service::handle);
    Handle::call_count = 0;

    Service service;
    Handle handle;

    handle(service, 1);                                  // lvalue reference
    handle(&service, 1);                                 // pointer
    handle(std::ref(service), 1);                        // reference_wrapper
    handle(std::make_unique<Service>(), 1);              // smart pointer
    handle(std::make_shared<Service>(), 1);              // shared pointer
    handle(Service{}, 1);                                // rvalue

    CHECK_EQ(Handle::call_count.load(), 6u);
    CHECK_EQ(service.calls_seen, 3);   // the three that touched this instance
}

TEST(lambda_counters_are_per_closure_type) {
    auto first = tempo::wrap([](int x) { return x + 1; });
    auto second = tempo::wrap([](int x) { return x + 1; });

    // Identical source, different closure types, therefore different counters.
    decltype(first)::call_count = 0;
    decltype(second)::call_count = 0;

    first(1);
    first(2);
    second(3);

    CHECK_EQ(decltype(first)::call_count.load(), 2u);
    CHECK_EQ(decltype(second)::call_count.load(), 1u);
}

TEST(std_function_instances_share_one_counter) {
    using Wrapped = tempo::Functor<std::function<int(int)>>;
    Wrapped::call_count = 0;

    Wrapped first{std::function<int(int)>{[](int x) { return x; }}};
    Wrapped second{std::function<int(int)>{[](int x) { return x * 2; }}};

    first(1);
    second(2);
    second(3);

    // Same type, so one counter -- this is the gotcha the README warns about.
    // Wrap the underlying lambda instead if you need them apart.
    CHECK_EQ(Wrapped::call_count.load(), 3u);
}

TEST(mutable_lambda_state_survives_across_calls) {
    auto counter = tempo::wrap([total = 0](int x) mutable { return total += x; });
    decltype(counter)::call_count = 0;

    CHECK_EQ(counter(1), 1);
    CHECK_EQ(counter(2), 3);
    CHECK_EQ(counter(3), 6);
    CHECK_EQ(decltype(counter)::call_count.load(), 3u);
}

TEST(reset_clears_counts_and_statistics) {
    using Metrics = TEMPO_CALLABLE_METRICS(add);
    Metrics::reset();

    Metrics metrics;
    metrics(1, 2);
    metrics(3, 4);

    auto before = Metrics::snapshot();
    CHECK_EQ(before.calls, 2u);
    CHECK_EQ(before.timed_calls, 2u);
    CHECK(before.has_samples);
    CHECK_GT(before.total_duration.count(), 0.0);

    Metrics::reset();

    auto after = Metrics::snapshot();
    CHECK_EQ(after.calls, 0u);
    CHECK_EQ(after.timed_calls, 0u);
    CHECK_EQ(after.max_depth, 0u);
    CHECK(!after.has_samples);
    CHECK_EQ(after.total_duration.count(), 0.0);
    CHECK_EQ(after.min_duration.count(), 0.0);
    CHECK_EQ(after.max_duration.count(), 0.0);
    CHECK_EQ(after.average_ms(), 0.0);
}

TEST(average_of_zero_calls_is_zero_not_a_division_by_zero) {
    using Metrics = TEMPO_CALLABLE_METRICS(multiply);
    Metrics::reset();

    auto empty = Metrics::snapshot();
    CHECK_EQ(empty.calls, 0u);
    CHECK_EQ(empty.average_ms(), 0.0);
    CHECK(!std::isnan(empty.average_ms()));
}

TEST(reset_all_clears_every_registered_metric) {
    using A = TEMPO_CALLABLE_METRICS(add);
    using M = TEMPO_CALLABLE_METRICS(multiply);

    A a;
    M m;
    a(1, 1);
    m(2, 2);
    CHECK_GT(A::snapshot().calls, 0u);
    CHECK_GT(M::snapshot().calls, 0u);

    tempo::report::reset_all();

    CHECK_EQ(A::snapshot().calls, 0u);
    CHECK_EQ(M::snapshot().calls, 0u);
}
