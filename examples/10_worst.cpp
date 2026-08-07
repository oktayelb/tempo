// 10 — The slowest calls, not just the slowest call
//
// 02_timing.cpp keeps the single worst input. One sample is an anecdote: the
// slowest call might have been a cold cache, a scheduler hiccup, or the one
// that paid for a lazy initialisation. A metric also keeps a ranking of the N
// slowest calls it has seen, arguments included, and a pattern across those N
// is what tells you which inputs are actually expensive.

#include "tempo.hpp"

#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

namespace impl {

// Stands in for real work whose cost depends on its arguments.
std::size_t run_query(const std::string& sql, int limit) {
    std::this_thread::sleep_for(std::chrono::microseconds(200 + limit * 4));
    return sql.size();
}

}  // namespace impl

// The third argument is the ranking's capacity. Without it the default of
// TEMPO_WORST_CALLS (10) applies.
TEMPO_INSTRUMENT(impl::run_query, run_query, 5);

int main() {
    const struct { const char* sql; int limit; } workload[] = {
        {"select id from orders where status = 'open'",        100},
        {"select * from orders join items using (order_id)",  5000},
        {"select count(*) from orders",                          1},
        {"select * from items where sku like '%-x'",          2500},
        {"select id from orders limit 10",                      10},
        {"select * from orders join items join shipments",     9000},
        {"select 1",                                             1},
        {"select * from audit_log",                           7500},
    };

    for (const auto& [sql, limit] : workload) { run_query(sql, limit); }

#if TEMPO_ENABLED
    // Slowest first, and no longer than the calls actually seen.
    std::cout << "the " << run_query.worst_calls().size()
              << " slowest of " << run_query.snapshot().calls << " calls\n\n";

    for (const auto& call : run_query.worst_calls()) {
        // The arguments are a tuple of the parameters as declared.
        const auto& [sql, limit] = call.args;

        std::cout << std::fixed << std::setprecision(3) << std::setw(8)
                  << call.duration.count() << " ms  limit=" << std::setw(5) << limit
                  << "  " << sql.substr(0, 40) << "\n";
    }

    // Reading them one by one races with other threads still calling. A
    // snapshot carries the ranking alongside the totals, all under one lock,
    // so the two always describe the same moment.
    const auto stats = run_query.snapshot();
    std::cout << "\nthe ranking's head is the slowest call: "
              << std::boolalpha
              << (stats.worst_calls()[0].duration == stats.max_duration) << "\n";

    // With the capacity at 0 nothing is ranked and worst_calls() stops
    // compiling, but fastest_args() and slowest_args() work as they always did.
#else
    std::cout << "built with TEMPO_ENABLED=0: run_query is a plain function "
                 "pointer, so there is no ranking to read.\n";
#endif
}
