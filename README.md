# tempo

A compile-time function abstractor built with template metaprogramming.

Point tempo at a function or method pointer and it gives you back a type that
knows the signature — return type, parameter types, arity, whether it is a
member, whether it is const — plus optional call counts, timings and the
argument values behind your slowest call. The wrapper returns the call
expression directly rather than through a named local, so the return value is
never copied or moved and even immovable return types pass through.

```cpp
#include "tempo.hpp"

namespace impl { int fibonacci(unsigned n); }

TEMPO_INSTRUMENT(impl::fibonacci, fibonacci);

fibonacci(26);
fibonacci(32);

std::get<0>(fibonacci.get_maximizers());   // 32 — the input that was slowest
```

Header-only. Requires C++20 (concepts, `std::source_location`, `inline static`).

## Instrumenting without touching call sites

`TEMPO_INSTRUMENT` names a function once, at its declaration, and leaves every
call site alone. A variable and a function cannot share a name in one scope but
they can across scopes, so the function lives in a nested namespace and the
wrapper takes its name outside; `fibonacci(26)` then resolves to the wrapper.

The call site is still recorded. For free functions and callable objects,
`operator()` is declared with the callable's exact parameter list plus a
trailing `std::source_location` that defaults to `current()` — and because that
parameter pack comes from the class template rather than being deduced from the
call, the default argument is evaluated at the caller. A deduced pack cannot do
this, which is why `TEMPO_METRICS_CALL` exists at all.

Define `TEMPO_ENABLED` as `0` before including and every instrumented name
collapses to a plain function pointer that the optimizer inlines away, so the
same source builds with tempo entirely absent.

Member functions cannot use the seam — `service.handle(...)` offers no free name
for a wrapper to shadow — so they keep the variadic `operator()` (which still
accepts `ClassName&`, `ClassName*`, `reference_wrapper` and smart pointers) and
`TEMPO_METRICS_CALL` for call-site locations.

## Recursion

A recursive call is resolved at compile time to the function itself and never
touches the pointer the wrapper holds, so no library can intercept it — the body
has to name the wrapper. `TEMPO_RECURSIVE` sets that up in one line:

```cpp
TEMPO_RECURSIVE(int, fibonacci, unsigned n) {
    return n < 2 ? n : TEMPO_SELF(fibonacci)(n-1) + TEMPO_SELF(fibonacci)(n-2);
}

fibonacci(24);   // an ordinary call site, as with TEMPO_INSTRUMENT
```

`TEMPO_SELF` is the whole switch. With `TEMPO_COUNT_RECURSION` at its default of
`0` it names the real function, so recursion costs nothing and only the outermost
call is counted — exactly what an untouched recursive function does. Set it to
`1` and it names the wrapper instead, and every level is counted:

| | calls | deepest | wall | tempo total |
|---|---|---|---|---|
| `TEMPO_COUNT_RECURSION=0` | 1 | 1 | 0.21 ms | 0.20 ms |
| `TEMPO_COUNT_RECURSION=1` | 150049 | 24 | 3.56 ms | 3.55 ms |

Off is the default because counting is not free: at roughly 82 ns per call,
routing 150k recursive calls through the wrapper turned a 0.21 ms computation
into 3.56 ms. Use it to answer "how many times does this really run", not to time
a hot recursion.

Timing stays correct in both modes because only the outermost call is timed.
Timing every level would sum intervals that contain one another — for
`fibonacci(22)` that reports about 69 ms of work for 4.7 ms of wall time. For the
same reason `average_ms()` divides by `timed_calls`, the outermost count, not by
every recursive step. `max_depth` records the deepest level reached, and the
report grows a `depth` column only when something actually recursed, so an
ordinary program prints the table it always did.

Depth is tracked per thread, so threads recursing independently do not disturb
each other, and it is restored correctly when a call throws. Only direct
recursion is covered; mutual recursion needs both functions instrumented, though
the timing is right either way. A return type containing a comma has to go behind
a type alias first — the preprocessor would split it.

`TEMPO_INSTRUMENT` is unaffected: it never routes recursion through the wrapper,
so an existing recursive function keeps measuring its base call only.

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
| `09_instrument.cpp` | instrument once, call sites unchanged |
| `10_recursion.cpp` | counting recursive calls, and the depth gate |

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
