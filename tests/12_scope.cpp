// TEMPO_SCOPE / TEMPO_SCOPE_NAMED.
//
// A scope timer has no type you can name -- the tag is a closure minted inside
// the macro -- so everything here reads its results the way a user would, out of
// tempo::report::collect(). Rows are matched by the name the macro recorded,
// which is why nearly every scope in this file is named: two expansions sharing
// a label produce two rows with that label, and lookup by name would not say
// which is which. That is exactly the trap TEMPO_SCOPE_NAMED exists to avoid,
// and it is pinned down in unnamed_scopes_in_one_function_share_a_label below.
//
// No absolute duration is asserted. What is checked is structure: one sample per
// block entry, min <= avg <= max, only the outermost entry of a recursive scope
// timed, a scope left by a throw not recorded at all.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

void burn(int rounds) {
    volatile unsigned int sink = 0;
    for (int i = 0; i < rounds * 2000; ++i) { sink = sink + static_cast<unsigned int>(i); }
}

// The row a named scope registered, or a zeroed row if it never ran.
tempo::report::Row row(std::string_view name) {
    for (const tempo::report::Row& candidate : tempo::report::collect()) {
        if (candidate.name == name) { return candidate; }
    }
    return tempo::report::Row{};
}

bool has_row(std::string_view name) {
    for (const tempo::report::Row& candidate : tempo::report::collect()) {
        if (candidate.name == name) { return true; }
    }
    return false;
}

int rows_containing(std::string_view fragment) {
    int found = 0;
    for (const tempo::report::Row& candidate : tempo::report::collect()) {
        if (candidate.name.find(fragment) != std::string::npos) { ++found; }
    }
    return found;
}

// At namespace scope: a local class may not have member templates.
template <typename T>
void instantiated(const char* label) {
    TEMPO_SCOPE_NAMED(label);
    burn(1);
}

int walk_down(int n) {
    TEMPO_SCOPE_NAMED("recursive");
    if (n <= 0) { return 0; }
    burn(1);
    return 1 + walk_down(n - 1);
}

}  // namespace

// A scope registers itself on first entry and not before.
TEST(scope_registers_on_first_entry) {
    CHECK(!has_row("never entered"));
    if (false) { TEMPO_SCOPE_NAMED("never entered"); }
    CHECK(!has_row("never entered"));

    { TEMPO_SCOPE_NAMED("entered once"); burn(1); }
    CHECK(has_row("entered once"));
    CHECK_EQ(row("entered once").calls, 1u);
}

// The destructor is what records, so a loop body yields one sample per pass.
TEST(loop_body_records_once_per_iteration) {
    for (int i = 0; i < 7; ++i) {
        TEMPO_SCOPE_NAMED("per iteration");
        burn(1);
    }

    const tempo::report::Row stats = row("per iteration");
    CHECK_EQ(stats.calls, 7u);
    CHECK_EQ(stats.timed_calls, 7u);
    CHECK(stats.has_samples);

    // Structural only: true however slow the machine is.
    CHECK_LE(stats.min_ms, stats.average_ms());
    CHECK_LE(stats.average_ms(), stats.max_ms);
    CHECK_LE(stats.max_ms, stats.total_ms);
}

// The same loop timed from outside is one sample, not N, and its interval
// contains all of the per-iteration ones.
TEST(scope_outside_the_loop_is_one_sample) {
    {
        TEMPO_SCOPE_NAMED("loop as a whole");
        for (int i = 0; i < 7; ++i) {
            TEMPO_SCOPE_NAMED("loop from inside");
            burn(1);
        }
    }

    CHECK_EQ(row("loop as a whole").calls, 1u);
    CHECK_EQ(row("loop from inside").calls, 7u);

    // An enclosing scope does not suppress an inner one: depth is per tag, so
    // the inner entries are outermost for their own tag and all get timed.
    CHECK_EQ(row("loop from inside").timed_calls, 7u);
    CHECK_LE(row("loop from inside").total_ms, row("loop as a whole").total_ms);
}

// Two scopes in one block, on one source line: the variable name is uniqued
// with __COUNTER__, so this compiles, and the tags are distinct closures, so
// the two sets of statistics are separate.
TEST(two_scopes_on_one_line_are_independent) {
    {
        TEMPO_SCOPE_NAMED("pair outer"); TEMPO_SCOPE_NAMED("pair inner");
        burn(1);
    }

    CHECK_EQ(row("pair outer").calls, 1u);
    CHECK_EQ(row("pair inner").calls, 1u);
    CHECK(row("pair outer").has_samples);
    CHECK(row("pair inner").has_samples);

    // Declared first, destroyed last: the outer interval contains the inner.
    CHECK_LE(row("pair inner").total_ms, row("pair outer").total_ms);
}

// The documented reason TEMPO_SCOPE_NAMED exists. Both scopes below are
// measured correctly and separately -- and both rows carry this function's
// name, so the report cannot tell you which is which.
TEST(unnamed_scopes_in_one_function_share_a_label) {
    { TEMPO_SCOPE(); burn(1); }
    { TEMPO_SCOPE(); burn(1); }

    CHECK_EQ(rows_containing("unnamed_scopes_in_one_function_share_a_label"), 2);
}

// Recursion: every entry counted, only the outermost timed, peak depth kept.
TEST(recursive_scope_times_only_the_outermost_entry) {
    CHECK_EQ(walk_down(5), 5);

    const tempo::report::Row stats = row("recursive");
    CHECK_EQ(stats.calls, 6u);          // n = 5, 4, 3, 2, 1, 0
    CHECK_EQ(stats.timed_calls, 1u);    // one interval, the outermost
    CHECK_EQ(stats.max_depth, 6u);
    CHECK_EQ(stats.min_ms, stats.max_ms);
}

// A scope left by a throw is counted as entered but is not a sample: the region
// never finished, so timing it would report a fraction of the work.
TEST(scope_left_by_a_throw_is_not_recorded) {
    try {
        TEMPO_SCOPE_NAMED("throwing scope");
        burn(1);
        throw std::runtime_error("no");
    } catch (const std::runtime_error&) {
    }

    const tempo::report::Row stats = row("throwing scope");
    CHECK_EQ(stats.calls, 1u);
    CHECK_EQ(stats.timed_calls, 0u);
    CHECK(!stats.has_samples);
    CHECK_EQ(stats.total_ms, 0.0);

    // The depth counter still came back down, so a later entry is outermost.
    { TEMPO_SCOPE_NAMED("after the throw"); burn(1); }
    CHECK_EQ(row("after the throw").timed_calls, 1u);
}

// break and continue are scope exits like any other.
TEST(early_exit_still_records) {
    for (int i = 0; i < 100; ++i) {
        TEMPO_SCOPE_NAMED("breaks out");
        if (i == 3) { break; }
    }
    CHECK_EQ(row("breaks out").calls, 4u);
    CHECK_EQ(row("breaks out").timed_calls, 4u);

    for (int i = 0; i < 5; ++i) {
        TEMPO_SCOPE_NAMED("continues");
        if (i % 2 == 0) { continue; }
        burn(1);
    }
    CHECK_EQ(row("continues").calls, 5u);
}

// The depth counter is thread_local and the statistics are behind a mutex, so
// the count is exact under contention.
TEST(entries_are_exact_across_threads) {
    constexpr int threads = 4;
    constexpr int per_thread = 250;

    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([] {
            for (int i = 0; i < per_thread; ++i) {
                TEMPO_SCOPE_NAMED("contended");
                burn(1);
            }
        });
    }
    for (std::thread& worker : pool) { worker.join(); }

    const tempo::report::Row stats = row("contended");
    CHECK_EQ(stats.calls, static_cast<tempo::CallCount>(threads * per_thread));
    CHECK_EQ(stats.timed_calls, static_cast<tempo::CallCount>(threads * per_thread));

    // Every entry was outermost on its own thread, so nothing looked nested.
    CHECK_EQ(stats.max_depth, 1u);
}

// A scope in a function template gets one tag, and so one row, per instantiation.
TEST(function_template_gets_a_row_per_instantiation) {
    instantiated<int>("template int");
    instantiated<int>("template int");
    instantiated<double>("template double");

    CHECK_EQ(row("template int").calls, 2u);
    CHECK_EQ(row("template double").calls, 1u);
}

// Last on purpose: reset_all() clears every registered metric, scopes included.
// The rows survive with zeroed numbers, and the scope still works afterwards.
TEST(reset_all_clears_scopes) {
    { TEMPO_SCOPE_NAMED("cleared"); burn(1); }
    CHECK_EQ(row("cleared").calls, 1u);

    tempo::report::reset_all();

    CHECK(has_row("cleared"));
    CHECK_EQ(row("cleared").calls, 0u);
    CHECK_EQ(row("cleared").timed_calls, 0u);
    CHECK_EQ(row("cleared").total_ms, 0.0);
    CHECK(!row("cleared").has_samples);

    // A distinct label, because this is a different expansion and so a
    // different row -- reusing "cleared" here would look up the one above.
    { TEMPO_SCOPE_NAMED("live after reset"); burn(1); }
    CHECK_EQ(row("live after reset").calls, 1u);
    CHECK(row("live after reset").has_samples);
}
