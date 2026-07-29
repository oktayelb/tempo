# tempo

A compile-time function abstractor built with template metaprogramming.

Point tempo at a function or method pointer and it gives you back a type that
knows the signature — return type, parameter types, arity, whether it is a
member, whether it is const — plus optional call counts, timings and the
argument values behind your slowest call. The wrapper takes forwarding
references and returns the call expression directly, so it adds no copies and
no moves of its own: the runtime cost is the metric you asked for and nothing
else.

```cpp
#include "tempo.hpp"

int fibonacci(unsigned n);

using Fib = TEMPO_CALLABLE_METRICS(fibonacci);
Fib fib;

TEMPO_METRICS_CALL(fib, 26);
TEMPO_METRICS_CALL(fib, 32);

std::get<0>(fib.get_maximizers());   // 32 — the input that was slowest
```

Header-only. Requires C++20 (concepts, `std::source_location`, `inline static`).

## Examples

```
cd examples && make run
```

| | |
|---|---|
| `01_traits.cpp` | signature introspection, all at compile time |
| `02_callables.cpp` | one abstraction over free functions and methods |
| `03_forwarding.cpp` | proof that the wrapper adds no copies |
| `04_metrics.cpp` | timings plus the arguments that produced them |
| `05_constructors.cpp` | counting object construction |
| `06_worst_input.cpp` | finding the input that made a function slow |
| `07_lambdas.cpp` | lambdas, functors and `std::function` |
| `08_report.cpp` | aggregated summary, quiet mode, threads |

Lambdas and functors are objects, not pointers, so they cannot be template
arguments. Use the factories instead of the macros:

```cpp
auto m = tempo::measure([](int rounds, int id) { /* ... */ return id; });
TEMPO_METRICS_CALL(m, 12, 302);
std::get<1>(m.get_maximizers());   // 302
```

## Reporting

Per-call output is on by default. Define `TEMPO_PRINT_ENABLED` as `0` before
including the header and every `cout` call leaves the build; statistics are
still collected, and one sorted summary comes from `tempo::report()`:

```
callable                         calls    total ms      avg ms      min ms      max ms
--------------------------------------------------------------------------------------
tempo::Callable<shared_worker>    2000      1.7875      0.0009      0.0002      0.0097
tempo::Callable<slow_path>           5      0.1220      0.0244      0.0083      0.0410
```

Every metric registers itself on its first call. `tempo::report_at_exit()`
prints the table when the program ends, and `tempo::reset_all()` clears
everything. Read statistics with `snapshot()`, which returns them all under one
lock so the numbers describe the same moment.

Statistics are mutex-guarded and safe to gather from several threads. The lock
is taken only after the clock has stopped, so it never inflates a measurement
and never serialises the code being profiled. State inside the callable itself
— a mutable lambda's captures, say — remains yours to synchronise.

## Known limits

Generic lambdas (`[](auto x){}`) and overloaded `operator()` are rejected — they
have no signature until called. `noexcept` free functions and overloaded
function names are not supported yet. Counters are static per wrapped type, so
every `std::function<int(int)>` in a program shares one counter; wrap the
underlying lambda instead. The summary reports totals and extremes but no
percentiles or histograms, since samples are not retained.
