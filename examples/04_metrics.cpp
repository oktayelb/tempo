// 04 — Timing, and which arguments produced it
//
// CallableMetrics times every call and keeps running totals. The part that
// ordinary profilers do not give you: it also remembers the *argument values*
// of the fastest and the slowest call, so a latency outlier comes with the
// input that caused it.
//
// Each call prints a report block, which is why the examples here use only a
// handful of calls. Aggregating instead of printing per call is the main thing
// this library still needs.

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
    const auto fastest = metrics.get_minimizers();
    const auto slowest = metrics.get_maximizers();
    using Metrics = std::remove_reference_t<decltype(metrics)>;

    std::cout << "\n--- " << label << " -----------------------------\n";
    std::cout << "calls    : " << Metrics::call_count << "\n";
    std::cout << "total    : " << Metrics::total_duration.count() << " ms\n";
    std::cout << "average  : " << Metrics::total_duration.count() / Metrics::call_count << " ms\n";
    std::cout << "fastest  : " << Metrics::min_duration.count() << " ms"
              << "  with args (" << std::get<0>(fastest) << ", " << std::get<1>(fastest) << ")\n";
    std::cout << "slowest  : " << Metrics::max_duration.count() << " ms"
              << "  with args (" << std::get<0>(slowest) << ", " << std::get<1>(slowest) << ")\n";
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
    assert(SleepMetrics::call_count == 0);
    assert(SleepMetrics::total_duration.count() == 0.0);
    std::cout << "after reset(): call_count = " << SleepMetrics::call_count << "\n";
}
