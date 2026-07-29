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

## Known limits

`noexcept` functions, overloaded names, lambdas and functors are not supported
yet. Reports are printed per call rather than aggregated, and that printing
happens inside the timed region — fine for millisecond workloads, not for
measuring anything that returns in nanoseconds.
