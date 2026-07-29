# tempo

Header-only C++20 instrumentation that remembers the arguments behind your
slowest call.

A profiler tells you *that* a function is slow. tempo tells you *what made it*
slow: it keeps the argument values of the fastest and slowest calls it has seen,
so the input that produced your worst case is still there when you go looking for
it.

```cpp
#include "tempo.hpp"

namespace impl { int fibonacci(unsigned n); }

TEMPO_INSTRUMENT(impl::fibonacci, fibonacci);

fibonacci(26);
fibonacci(32);

std::get<0>(fibonacci.get_maximizers());   // 32 — the input that was slowest
```

Point tempo at a function or method pointer and you also get a type that knows
the signature — return type, parameter types, arity, whether it is a member,
whether it is const — along with call counts and timings. The wrapper returns the
call expression directly rather than through a named local, so the return value
is never copied or moved and even immovable return types pass through.

MIT licensed. Header-only, nothing to link. Requires C++20 (concepts,
`std::source_location`, `inline static`).

## Building

Drop `tempo.hpp` on your include path. There is nothing to link.

```sh
# GCC
g++   -std=c++20 -O2 -pthread -I/path/to/tempo your.cpp -o your_program

# Clang
clang++ -std=c++20 -O2 -pthread -I/path/to/tempo your.cpp -o your_program

# Apple Clang (macOS)
clang++ -std=c++20 -O2 -I/path/to/tempo your.cpp -o your_program

# MSVC
cl /std:c++20 /Zc:preprocessor /EHsc /O2 /I path\to\tempo your.cpp
```

`/Zc:preprocessor` is **required** on MSVC, not optional: `TEMPO_METRICS_CALL`
and `TEMPO_PROFILE_CALL` use `__VA_OPT__`, which the traditional MSVC
preprocessor does not implement. `std::source_location` needs VS 2019 16.10 or
newer.

`-pthread` is required wherever your standard library needs it for `<mutex>` and
`<thread>`; on glibc 2.34 and later it links without, but pass it anyway.

### Flags that change behaviour

| Flag | Effect |
|---|---|
| `-O0` vs `-O2` | Per-call overhead roughly doubles unoptimized: **169 ns** at `-O0` against **83 ns** at `-O2`. Every overhead number quoted in this README is `-O2`. Measure at the level you ship. |
| `-fno-exceptions` | Compiles and works. The guards that skip recording for a throwing call become dead code, since nothing can throw. |
| `-fno-rtti` | No effect. tempo reads `__PRETTY_FUNCTION__`, never `typeid`. |
| `-flto` | No effect on correctness. |
| `-std=c++23` | Builds and passes the suite. C++20 is the floor, not a ceiling. |

### The three macros are ODR-sensitive

`TEMPO_ENABLED`, `TEMPO_PRINT_ENABLED` and `TEMPO_COUNT_RECURSION` change the
bodies of inline functions and templates, and `TEMPO_ENABLED` changes the *type*
an instrumented name has. Defining them differently in two translation units of
the same program is an ODR violation, and the linker will not warn you.

Set them on the compiler command line so every translation unit agrees:

```sh
g++ -std=c++20 -O2 -pthread -DTEMPO_PRINT_ENABLED=0 -DTEMPO_COUNT_RECURSION=1 ...
```

If you define them in source instead, they must come before `#include
"tempo.hpp"` in every file, with the same values.

### Compiler-dependent output

`tempo::report()` names each row by scraping `__PRETTY_FUNCTION__`, whose
spelling is not standardised. A function in an anonymous namespace prints as
`{anonymous}::f` under GCC and `&(anonymous namespace)::f` under Clang, and the
name column is capped at 60 characters, so the longer Clang spelling may be
truncated. Do not parse the report; read `snapshot()` instead.

### What CI covers

GCC and Clang on Linux and Apple Clang on macOS, under every combination of the
three macros, at C++20 and C++23, plus AddressSanitizer, UndefinedBehaviorSanitizer
and ThreadSanitizer. **MSVC is not covered** — the code paths for it exist and are
written against the documented behaviour, but nothing verifies them.

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

## Tests

```
cd tests && make run          # the whole suite
make matrix                   # every combination of the macros
make diagnostics              # the errors, checked for being one line and readable
make sanitize                 # address + undefined behaviour
make tsan                     # data races
```

Each file is a separate binary, like the examples, and exits non-zero on
failure. 109 tests, 369 checks.

| | |
|---|---|
| `01_traits.cpp` | signature introspection, almost entirely `static_assert` |
| `02_counting.cpp` | counters, the per-type sharing rule, reset |
| `03_forwarding.cpp` | copies and moves counted exactly, immovable returns |
| `04_metrics.cpp` | timing invariants, extremes, snapshot coherence |
| `05_location.cpp` | call-site capture, checked against `__LINE__` |
| `06_recursion.cpp` | the depth gate, both counting modes, throwing recursion |
| `07_exceptions.cpp` | throwing calls, nested unwinding, failed construction |
| `08_threads.cpp` | exact counts under contention, torn-read detection |
| `09_constructors.cpp` | `ConstructorProfiler`, elision, move-only arguments |
| `10_abuse.cpp` | degenerate signatures, 16 parameters, nesting, mid-run reset |
| `diagnostics/` | 14 mistakes that must NOT compile, each with one clear message |

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

## When you use it wrong

Everything tempo rejects, it rejects with a sentence, not with a template
instantiation backtrace. Pointing `TEMPO_INSTRUMENT` at a `noexcept` function
used to produce 71 lines of errors ending in `invalid use of incomplete type`.
It now produces one:

```
error: static assertion failed: tempo: this function's type is not supported.
  It is declared 'noexcept', and tempo does not wrap noexcept callables yet.
  Wrapping one would have to drop the noexcept -- the wrapper itself is not
  noexcept -- which silently changes the type your callers see.
  Fix: wrap the call in a lambda and measure that instead --
      auto m = tempo::measure([](int a){ return my_noexcept_fn(a); });
  The function you are measuring stays exactly as it is.
```

Each message names what was wrong and what to write instead. The cases covered:

| | |
|---|---|
| `noexcept` function or method | says so, and shows the lambda that works |
| C-style variadic (`printf`-like) | says so, and why the `...` cannot be kept |
| ref-qualified or volatile member | says which qualifier is the problem |
| lambda passed to the macros | points at `tempo::measure` instead |
| generic lambda, or overloaded `operator()` | explains that it has no signature until called |
| `tempo::Metrics<MyLambda>` | names the wrapper type that was meant |
| wrong arguments at a call site | reminds you the instance comes first for a method |
| `get_maximizers()` with unstorable args | explains which parameter disabled capture |
| `ConstructorProfiler<int>` | says it needs a class |

The one-error guarantee is enforced, not hoped for: `tests/diagnostics` compiles
each mistake and fails if it produces more than one error, or an error that does
not carry tempo's own wording. Both compilers run it in CI, because they do not
agree on which unsupported shapes they will silently accept — Clang deduces a
plain signature from a `noexcept` function and drops the qualifier, GCC refuses
outright. tempo rejects it either way, with the same message.

## Known limits

`noexcept` callables, C-style variadic functions, generic lambdas
(`[](auto x){}`), overloaded `operator()` and overloaded function names are not
supported — all of them diagnosed as above rather than left to the compiler.
Counters are static per wrapped type, so every `std::function<int(int)>` in a
program shares one counter; wrap the underlying lambda instead. The summary
reports totals and extremes but no percentiles or histograms, since samples are
not retained.
