// 09 — Timing a block instead of a callable
//
// Everything else in tempo wraps something callable: you hand it a function
// pointer or a lambda and call through the wrapper. TEMPO_SCOPE is the other
// direction. It times the braces it stands in -- one line inside a body you
// already have, no wrapper object, no call site touched, nothing moved into
// another namespace.
//
// That makes it the only form that reaches code with no callable to point at:
// a constructor body, half of a function, one branch, one loop body.
//
// It is a destructor that stops the clock, so the object dying at the end of
// the scope is the mechanism, not a problem -- the statistics are static and
// outlive it. In a loop body that means one sample per iteration.
//
//   TEMPO_SCOPE()             names the row after the enclosing function
//   TEMPO_SCOPE_NAMED("...")  names it yourself -- REQUIRED for two scopes in
//                             the same function, see parse_and_write below
//
// Built with -DTEMPO_ENABLED=0 the macro expands to nothing at all: no object,
// no statics, the block is left exactly as written.

#include "tempo.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

// Stands in for real work. Unsigned because the running sum passes INT_MAX and
// signed overflow is undefined.
void burn(int rounds) {
    volatile unsigned int sink = 0;
    for (int i = 0; i < rounds * 4000; ++i) { sink = sink + static_cast<unsigned int>(i); }
}

// One scope over a whole function: the row is named "void {anonymous}::load()"
// or so, straight from source_location::function_name().
void load() {
    TEMPO_SCOPE();
    burn(3);
}

// TWO scopes in one function, so both are named by hand. With TEMPO_SCOPE()
// twice they would still be measured separately -- each expansion has its own
// statistics -- but both rows would carry this function's name and nothing in
// the report would tell them apart.
void parse_and_write(const std::vector<std::string>& lines) {
    {
        TEMPO_SCOPE_NAMED("parse");
        for (const std::string& line : lines) { burn(static_cast<int>(line.size())); }
    }
    {
        TEMPO_SCOPE_NAMED("write");
        burn(2);
    }
}

// A scope inside a recursive function is re-entered before it is left. Every
// entry is counted; only the outermost is timed, so the total is the wall clock
// rather than a sum of intervals that contain one another. The report grows a
// depth column when that happens.
int walk(int n) {
    TEMPO_SCOPE_NAMED("walk");
    if (n <= 0) { return 0; }
    burn(1);
    return 1 + walk(n - 1);
}

}  // namespace

int main() {
    const std::vector<std::string> lines{"alpha", "beta", "gamma delta"};

    load();
    load();
    parse_and_write(lines);

    // Per iteration: the timer is built and destroyed on every pass, so "job"
    // gets one sample each time round. avg is the cost of one iteration, and
    // max is the worst single one.
    for (int job = 1; job <= 6; ++job) {
        TEMPO_SCOPE_NAMED("job");
        burn(job);
    }

    // The same loop measured as one interval instead: put the scope outside it.
    // Both forms can coexist -- separate scopes are separate statistics, and an
    // enclosing scope never suppresses an inner one.
    {
        TEMPO_SCOPE_NAMED("all jobs");
        for (int job = 1; job <= 6; ++job) { burn(job); }
    }

    // Leaving early still records: break, continue and return are scope exits,
    // so the destructor runs.
    for (int i = 0; i < 100; ++i) {
        TEMPO_SCOPE_NAMED("stops early");
        burn(1);
        if (i == 2) { break; }
    }

    std::cout << "walked " << walk(5) << " steps\n";

    // Scope rows land in the same table as every wrapper, sorted by total time.
    tempo::report::print();

#if !TEMPO_ENABLED
    std::cout << "built with TEMPO_ENABLED=0: every TEMPO_SCOPE above expanded "
                 "to nothing, so there is nothing to report.\n";
#endif
}
