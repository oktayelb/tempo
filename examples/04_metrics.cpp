// 04 — Timing, and which arguments produced it
//
// CallableMetrics times every call and keeps running totals. The part that
// ordinary profilers do not give you: it also remembers the *argument values*
// of the fastest and the slowest call, so a latency outlier comes with the
// input that caused it.
//
// Nothing is printed per call: TEMPO_PRINT_ENABLED defaults to 0, so the
// statistics are collected quietly and read back through the accessors below,
// or all at once through tempo::report(). Build this with
// -DTEMPO_PRINT_ENABLED=1 to watch each call narrate itself instead.

#include "tempo.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

int sleep_for(int milliseconds, int request_id) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return request_id;
}

struct Service {
    int handle(int milliseconds, int request_id) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        return request_id;
    }
    int handle_const(int milliseconds, int request_id) const {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        return request_id;
    }
};

void report(const char* label, auto& metrics) {
    using MetricsType = std::remove_reference_t<decltype(metrics)>;

    // One consistent view of every statistic, taken under a single lock.
    // Reading total and min separately would not just be a data race, it would
    // be inconsistent: one could come from before an update and one from after.
    const auto stats = MetricsType::snapshot();

    std::cout << "\n--- " << label << " -----------------------------\n";
    std::cout << "calls    : " << stats.calls << "\n";
    std::cout << "total    : " << stats.total_duration.count() << " ms\n";
    std::cout << "average  : " << stats.average_ms() << " ms\n";
    std::cout << "fastest  : " << stats.min_duration.count() << " ms"
              << "  with args (" << std::get<0>(stats.min_args)
              << ", " << std::get<1>(stats.min_args) << ")\n";
    std::cout << "slowest  : " << stats.max_duration.count() << " ms"
              << "  with args (" << std::get<0>(stats.max_args)
              << ", " << std::get<1>(stats.max_args) << ")\n";
}

int main() {
    // --- a free function ------------------------------------------------------
    using SleepMetrics = TEMPO_CALLABLE_METRICS(sleep_for);
    SleepMetrics::reset();
    SleepMetrics sleep_metrics;

    // TEMPO_METRICS_CALL records the *caller's* file and line, which a plain
    // sleep_metrics(...) call cannot do.
    TEMPO_METRICS_CALL(sleep_metrics, 12, 101);
    TEMPO_METRICS_CALL(sleep_metrics, 4, 102);
    TEMPO_METRICS_CALL(sleep_metrics, 20, 103);

    report("sleep_for", sleep_metrics);
    assert(std::get<1>(sleep_metrics.get_minimizers()) == 102);  // the 4 ms call
    assert(std::get<1>(sleep_metrics.get_maximizers()) == 103);  // the 20 ms call
    std::cout << "request 103 was the slow one, and tempo kept its arguments.\n";

    // --- a member function ----------------------------------------------------
    using HandleMetrics = TEMPO_CALLABLE_METRICS(Service::handle);
    HandleMetrics::reset();
    HandleMetrics handle_metrics;

    Service service;
    TEMPO_METRICS_CALL(handle_metrics, service, 10, 201);
    TEMPO_METRICS_CALL(handle_metrics, service, 3, 202);
    TEMPO_METRICS_CALL(handle_metrics, service, 16, 203);

    // The instance is not part of ArgsType, so the recorded tuple holds only
    // the declared parameters.
    static_assert(std::tuple_size_v<HandleMetrics::StoredArgsType> == 2);
    report("Service::handle", handle_metrics);

    // --- a const member function ---------------------------------------------
    using ConstMetrics = TEMPO_CALLABLE_METRICS(Service::handle_const);
    ConstMetrics::reset();
    ConstMetrics const_metrics;

    const Service frozen;
    TEMPO_METRICS_CALL(const_metrics, frozen, 2, 301);
    TEMPO_METRICS_CALL(const_metrics, frozen, 7, 302);
    report("Service::handle_const", const_metrics);

    // --- where was it last called from? ---------------------------------------
    const auto location = SleepMetrics::get_last_call_location();
    std::cout << "\nlast sleep_for call site: "
              << location.file_name() << ":" << location.line()
              << " in " << location.function_name() << "\n";

    // --- reset() clears counters, totals and recorded arguments ---------------
    SleepMetrics::reset();
    const auto cleared = SleepMetrics::snapshot();
    assert(cleared.calls == 0);
    assert(cleared.total_duration.count() == 0.0);
    assert(!cleared.has_samples);
    std::cout << "after reset(): call_count = " << cleared.calls << "\n";
}
