// 08 — Concurrency
//
// Counters are atomic and the statistics are mutex-guarded, so totals must be
// exact under contention rather than approximately right. These tests are also
// the ones the CI sanitizer jobs care about: run under ThreadSanitizer they
// prove the absence of races, not just the presence of plausible numbers.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <atomic>
#include <barrier>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int spin(int rounds, int tag) {
    volatile int sink = 0;
    for (int i = 0; i < rounds; ++i) { sink += i; }
    return tag;
}

int trivial(int x) { return x; }

// Records the largest number of threads that were ever inside it at once. Used
// to observe concurrency directly instead of guessing at it from elapsed time.
std::atomic<int> currently_inside{0};
std::atomic<int> peak_concurrency{0};

int observed(int milliseconds, int tag) {
    const int now = currently_inside.fetch_add(1, std::memory_order_relaxed) + 1;
    int seen = peak_concurrency.load(std::memory_order_relaxed);
    while (now > seen &&
           !peak_concurrency.compare_exchange_weak(seen, now,
                                                   std::memory_order_relaxed)) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    currently_inside.fetch_sub(1, std::memory_order_relaxed);
    return tag;
}

// Starts every thread at the same instant, so the contention is real rather
// than the threads politely taking turns.
template <typename Body>
void run_together(int thread_count, Body body) {
    std::barrier start{thread_count};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(thread_count));
    for (int i = 0; i < thread_count; ++i) {
        workers.emplace_back([&start, &body, i] {
            start.arrive_and_wait();
            body(i);
        });
    }
    for (auto& worker : workers) { worker.join(); }
}

}  // namespace

TEST(call_counts_are_exact_under_contention) {
    using Metrics = TEMPO_CALLABLE_METRICS(trivial);
    Metrics::reset();
    Metrics metrics;

    constexpr int threads = 8;
    constexpr int per_thread = 500;

    run_together(threads, [&metrics](int) {
        for (int i = 0; i < per_thread; ++i) { metrics(i); }
    });

    // Exact, not approximate. A lost update here would be a real bug.
    CHECK_EQ(Metrics::snapshot().calls, unsigned{threads * per_thread});
    CHECK_EQ(Metrics::snapshot().timed_calls, unsigned{threads * per_thread});
}

TEST(statistics_stay_internally_consistent_under_contention) {
    using Metrics = TEMPO_CALLABLE_METRICS(spin);
    Metrics::reset();
    Metrics metrics;

    constexpr int threads = 8;
    constexpr int per_thread = 200;

    run_together(threads, [&metrics](int id) {
        for (int i = 0; i < per_thread; ++i) { metrics(50 + i, id); }
    });

    // One lock, one coherent view: these relationships cannot be torn.
    const auto stats = Metrics::snapshot();
    CHECK_EQ(stats.calls, unsigned{threads * per_thread});
    CHECK_LE(stats.min_duration.count(), stats.max_duration.count());
    CHECK_LE(stats.min_duration.count(), stats.average_ms());
    CHECK_LE(stats.average_ms(), stats.max_duration.count());
    CHECK_GE(stats.total_duration.count(), stats.max_duration.count());
    CHECK(stats.has_samples);
    CHECK_EQ(stats.max_depth, 1u);
}

TEST(repeated_snapshots_from_many_threads_are_never_torn) {
    using Metrics = TEMPO_CALLABLE_METRICS(spin);
    Metrics::reset();
    Metrics metrics;

    std::atomic<bool> stop{false};
    std::atomic<int> torn{0};

    // Readers hammer snapshot() while writers keep calling.
    std::vector<std::thread> readers;
    for (int i = 0; i < 3; ++i) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                const auto stats = Metrics::snapshot();
                if (stats.has_samples) {
                    if (stats.min_duration > stats.max_duration) { ++torn; }
                    if (stats.total_duration < stats.max_duration) { ++torn; }
                    if (stats.timed_calls > stats.calls) { ++torn; }
                }
            }
        });
    }

    run_together(4, [&metrics](int id) {
        for (int i = 0; i < 300; ++i) { metrics(40, id); }
    });

    stop.store(true);
    for (auto& reader : readers) { reader.join(); }

    CHECK_EQ(torn.load(), 0);
}

TEST(the_lock_is_not_held_across_the_measured_call) {
    // If the mutex were held while the user's function ran, calls would
    // serialise and no two threads could ever be inside it at once.
    //
    // This observes the overlap directly rather than inferring it from elapsed
    // time. A wall-clock threshold would be a bet on how loaded the machine is,
    // and CI machines are shared -- whereas threads that genuinely run
    // concurrently overlap whether the box is idle or hammered.
    using Metrics = TEMPO_CALLABLE_METRICS(observed);
    Metrics::reset();
    peak_concurrency.store(0);
    Metrics metrics;

    run_together(4, [&metrics](int id) { metrics(30, id); });

    CHECK_EQ(Metrics::snapshot().calls, 4u);

    // Serialised execution would pin this at exactly 1.
    CHECK_GT(peak_concurrency.load(), 1);
}

TEST(distinct_metrics_do_not_interfere_across_threads) {
    using A = TEMPO_CALLABLE_METRICS(trivial);
    using B = TEMPO_CALLABLE_METRICS(spin);
    A::reset();
    B::reset();

    A a;
    B b;

    run_together(6, [&a, &b](int id) {
        for (int i = 0; i < 200; ++i) {
            if (id % 2 == 0) { a(i); } else { b(20, id); }
        }
    });

    CHECK_EQ(A::snapshot().calls, 3u * 200u);
    CHECK_EQ(B::snapshot().calls, 3u * 200u);
}

TEST(report_can_be_generated_while_calls_are_in_flight) {
    tempo::reset_all();

    using Metrics = TEMPO_CALLABLE_METRICS(spin);
    Metrics metrics;

    std::atomic<bool> stop{false};
    std::thread reporter{[&] {
        while (!stop.load(std::memory_order_relaxed)) {
            std::ostringstream out;
            tempo::report(out);
        }
    }};

    run_together(4, [&metrics](int id) {
        for (int i = 0; i < 250; ++i) { metrics(30, id); }
    });

    stop.store(true);
    reporter.join();

    CHECK_EQ(Metrics::snapshot().calls, 4u * 250u);
}

TEST(a_shared_functor_is_safe_when_its_own_state_is) {
    // tempo guards its own statistics. State inside the callable stays the
    // caller's responsibility, so this lambda uses an atomic.
    std::atomic<int> total{0};
    auto metrics = tempo::measure([&total](int amount) {
        return total.fetch_add(amount, std::memory_order_relaxed) + amount;
    });
    decltype(metrics)::reset();

    run_together(4, [&metrics](int) {
        for (int i = 0; i < 250; ++i) { metrics(1); }
    });

    CHECK_EQ(total.load(), 1000);
    CHECK_EQ(decltype(metrics)::snapshot().calls, 1000u);
}

TEST(concurrent_construction_counting_is_exact) {
    struct Widget {
        int value;
        explicit Widget(int v) : value(v) {}
    };
    using Maker = tempo::ConstructorProfiler<Widget>;
    Maker::obj_count = 0;
    Maker make;

    run_together(6, [&make](int id) {
        for (int i = 0; i < 300; ++i) { (void)make(id); }
    });

    CHECK_EQ(Maker::obj_count.load(), 6u * 300u);
}

TEST(depth_is_zero_on_every_thread_after_the_work_is_done) {
    using Metrics = TEMPO_CALLABLE_METRICS(trivial);
    Metrics::reset();
    Metrics metrics;

    run_together(4, [&metrics](int) {
        for (int i = 0; i < 100; ++i) { metrics(i); }
        // Each thread's own depth must have unwound to zero.
        if (Metrics::current_depth() != 0) { FAIL("depth left non-zero on a worker"); }
    });

    CHECK_EQ(Metrics::current_depth(), 0u);
}
