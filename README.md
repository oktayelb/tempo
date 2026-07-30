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

auto [slowest_n] = fibonacci.slowest_args();   // 32 — the input that was slowest
```

Point tempo at a function or method pointer and you also get a type that knows
the signature — return type, parameter types, arity, whether it is a member,
whether it is const — along with call counts and timings. The wrapper returns the
call expression directly rather than through a named local, so the return value
is never copied or moved and even immovable return types pass through.

MIT licensed. Header-only, nothing to link. Requires C++20 (concepts,
`std::source_location`, `inline static`).

## Building

Drop `tempo.hpp` on your include path. There is nothing to link, nothing to
configure, and no build system of tempo's to adopt — it is one header, and your
own build already knows how to compile a header.

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

### Version

The header defines its own version, so code that vendors a copy can pin the
revision it was written against:

```cpp
#if !defined(TEMPO_VERSION) || TEMPO_VERSION < 10000
#error "this code needs tempo 1.0.0 or newer"
#endif
```

`TEMPO_VERSION` is `major * 10000 + minor * 100 + patch`, which orders correctly
across all three fields; `TEMPO_VERSION_MAJOR`, `_MINOR`, `_PATCH` and
`TEMPO_VERSION_STRING` are there too. The major number stays at `0` while the
API is still free to change.

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

`tempo::report::print()` names each row by scraping `__PRETTY_FUNCTION__`, whose
spelling is not standardised. A function in an anonymous namespace prints as
`{anonymous}::f` under GCC and `&(anonymous namespace)::f` under Clang, and the
name column is capped at 60 characters, so the longer Clang spelling may be
truncated. Do not parse the report; read `snapshot()` instead.

### What CI covers

GCC and Clang on Linux, under every combination of the three macros, at C++20
and C++23, plus AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer,
over the tests and the examples alike.

**Linux is the only platform covered.** macOS and MSVC are not: the code paths
for both exist and are written against the documented behaviour, and the macOS
build is expected to work, but nothing verifies either. The macOS job is still
in `.github/workflows/ci.yml`, commented out, ready to be switched back on.

## Instrumenting without touching call sites

`TEMPO_INSTRUMENT` names a function once, at its declaration, and leaves every
call site alone. A variable and a function cannot share a name in one scope but
they can across scopes, so the function lives in a nested namespace and the
wrapper takes its name outside; `fibonacci(26)` then resolves to the wrapper.

The call site is still recorded. For free functions and callable objects,
`Metrics::operator()` is declared with the callable's exact parameter list plus a
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

### When you need the call macros

Less often than it looks. A plain call captures the caller wherever the wrapper
can be given a fixed signature, which is everything `tempo::measure` wraps except
member functions:

```cpp
auto m = tempo::measure([](int rounds, int id) { /* ... */ return id; });
m(12, 302);                    // call site captured, no macro

TEMPO_CALLABLE_METRICS(fibonacci) fib;
fib(32);                       // likewise for a free function
```

| | plain call | needs the macro |
|---|---|---|
| `Metrics` over a free function | captures the caller | — |
| `Metrics` over a lambda or functor | captures the caller | — |
| `Metrics` over a member function | counted and timed, location is `tempo.hpp` | `TEMPO_METRICS_CALL` |
| `Profiler` (any callable) | location is `tempo.hpp` | `TEMPO_PROFILE_CALL` |

`Profiler` is the exception across the board: its `operator()` is variadic for
every callable kind and calls `source_location::current()` in its own body, so
the location it records is the header's, not yours. Counting is unaffected —
only the reported call site is. Use `TEMPO_PROFILE_CALL` whenever the location
matters, or `tempo::measure` instead, which does not need it.

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

The macro declares the real function under a suffixed name — `TEMPO_TARGET(fibonacci)`,
which expands to `fibonacci_tempo_target` — points a wrapper at it under the plain
name, and then opens the real definition, which is why the body you write follows
the macro directly. You never need to write `TEMPO_TARGET` yourself; it is the
name `TEMPO_SELF` resolves to when recursion counting is off.

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

One use case per file, kept short. The exhaustive version of each — every edge
case, proved rather than shown — is in `tests/`.

| | |
|---|---|
| `01_counting.cpp` | counting calls to a function or a method |
| `02_timing.cpp` | timings, and the argument values of the slowest call |
| `03_lambdas.cpp` | lambdas and functors, through the factories |
| `04_instrument.cpp` | instrument once, call sites unchanged |
| `05_report.cpp` | one sorted summary of everything that ran |
| `06_recursion.cpp` | counting recursive calls, and the depth gate |
| `07_constructors.cpp` | counting object construction |
| `08_traits.cpp` | reading a signature at compile time |

## Tests

```
cd tests && make run          # the whole suite
make matrix                   # every combination of the macros
make diagnostics              # the errors, checked for being one line and readable
make sanitize                 # address + undefined behaviour
make tsan                     # data races
```

The examples have the same three: `cd examples && make run`, `make sanitize`,
`make tsan`.

Each file is a separate binary, like the examples, and exits non-zero on
failure. 126 tests, 398 checks.

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
| `11_noexcept.cpp` | the qualifier surviving the wrapper, and what capture costs |
| `diagnostics/` | 14 mistakes that must NOT compile, each with one clear message |

Lambdas and functors are objects, not pointers, so they cannot be template
arguments. Use the factories instead of the macros:

```cpp
auto m = tempo::measure([](int rounds, int id) { /* ... */ return id; });
m(12, 302);
auto [slowest_rounds, slowest_id] = m.slowest_args();   // 12, 302
```

## Reporting

tempo is quiet by default. Statistics are collected on every call and read back
through the accessors, or all at once as one sorted summary from
`tempo::report::print()`:

```
callable                         calls    total ms      avg ms      min ms      max ms
--------------------------------------------------------------------------------------
tempo::Callable<shared_worker>    2000      1.7875      0.0009      0.0002      0.0097
tempo::Callable<slow_path>           5      0.1220      0.0244      0.0083      0.0410
```

Every metric registers itself on its first call. `tempo::report::at_exit()`
prints the table when the program ends, and `tempo::report::reset_all()` clears
everything. Read statistics with `snapshot()`, which returns them all under one
lock so the numbers describe the same moment.

Define `TEMPO_PRINT_ENABLED` as `1` before including the header to get a block
of lines on every call instead — the clearer view when you are watching a
handful of calls, and unusable on anything called often. It is off by default
because the printing happens under the same lock as the recording.

Statistics are mutex-guarded and safe to gather from several threads. The lock
is taken only after the clock has stopped, so it never inflates a measurement
and never serialises the code being profiled. State inside the callable itself
— a mutable lambda's captures, say — remains yours to synchronise.

## When you use it wrong

Everything tempo rejects, it rejects with a sentence, not with a template
instantiation backtrace. Pointing `TEMPO_INSTRUMENT` at a `printf`-like variadic
function used to produce a page of errors ending in `invalid use of incomplete
type`. It now produces one:

```
error: static assertion failed: tempo: this function's type is not supported.
  tempo matches function pointers of the form  ret(*)(args...), with or
  without 'noexcept'.
  A function declared with a trailing '...' (printf-like) cannot be wrapped
  faithfully: tempo would build a wrapper from the NAMED parameters only and
  quietly drop the '...', so calls passing variadic arguments would stop
  compiling and the signature would no longer be the one you wrote.
  Fix: wrap the calls you want measured in a lambda --
      auto m = tempo::measure([]{ return my_printf_like(3, 10, 20, 30); });
```

Each message names what was wrong and what to write instead. The cases covered:

| | |
|---|---|
| C-style variadic (`printf`-like) | says so, and why the `...` cannot be kept |
| ref-qualified or volatile member | says which qualifier is the problem |
| lambda passed to the macros | points at `tempo::measure` instead |
| generic lambda, or overloaded `operator()` | explains that it has no signature until called |
| `tempo::Metrics<MyLambda>` | names the wrapper type that was meant |
| wrong arguments at a call site | reminds you the instance comes first for a method |
| `slowest_args()` with unstorable args | explains which parameter disabled capture |
| `ConstructorProfiler<int>` | says it needs a class |

The one-error guarantee is enforced, not hoped for: `tests/diagnostics` compiles
each mistake and fails if it produces more than one error, or an error that does
not carry tempo's own wording. Both compilers run it in CI, because they do not
agree on which unsupported shapes they will silently accept.

## noexcept

A `noexcept` callable gets a `noexcept` wrapper. The qualifier is read off the
callable and reapplied to every call operator on the way out, so an instrumented
name still satisfies `noexcept(f(x))` and nothing that depended on the guarantee
changes meaning:

```cpp
namespace impl { int scale(int v) noexcept { return v * 2; } }
TEMPO_INSTRUMENT(impl::scale, scale);

static_assert(noexcept(scale(1)));   // still true through the wrapper
```

This matters more than it looks, because `TEMPO_INSTRUMENT` works by giving the
wrapper the function's own name. The wrapper's type *is* the type every call site
now resolves against, so dropping the `noexcept` there would silently weaken the
contract everywhere, with no diagnostic anywhere.

It costs one thing. Capturing an argument means copying it, and a copy that
allocates can throw — inside a wrapper that has just promised it will not. tempo
will not turn a `noexcept` function into one that terminates on a full heap, so
for `noexcept` callables argument capture is restricted to parameter types it can
copy, store and overwrite without throwing:

```cpp
std::size_t width(std::string) noexcept;   // tracks_args == false
int         scale(int)         noexcept;   // tracks_args == true
```

Timing, call counts and the report are unaffected either way, and `tracks_args`
reports it at compile time — asking for `slowest_args()` on a metric that is
not capturing is a compile error that says why. To get capture back, measure a
plain (throwing) lambda that calls the function.

Two residual notes. `TEMPO_RECURSIVE` has no slot for the qualifier, so a
recursive `noexcept` function cannot be declared through it. And an allocation
failure inside the once-per-metric registration terminates rather than unwinds,
which is what any `noexcept` function does when it runs out of memory.

## Known limits

C-style variadic functions, generic lambdas (`[](auto x){}`), overloaded
`operator()` and overloaded function names are not supported — all of them
diagnosed as above rather than left to the compiler. Counters are static per
wrapped type, so every `std::function<int(int)>` in a program shares one counter;
wrap the underlying lambda instead. The summary reports totals and extremes but
no percentiles or histograms, since samples are not retained. `noexcept`
callables are supported, with the argument-capture restriction described above.
