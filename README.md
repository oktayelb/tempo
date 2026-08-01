# tempo

Header-only C++20 instrumentation that remembers the arguments behind your
slowest call.

A profiler tells you *that* a function is slow. tempo tells you *what made it*
slow: it keeps the argument values of the fastest and slowest calls it has seen,
so the input that produced your worst case is still there when you go looking for
it.

```cpp
#include "tempo.hpp"

std::size_t run_query(const std::string& sql, int limit);   // your code, unchanged

tempo::CallableMetrics<&run_query> query;                   // one object, no macro

void nightly_job() {
    query("select id from orders where status = 'open'", 100);
    query("select * from orders join items using (order_id)", 5000);

    // Ordinary calls, all timed. The arguments of the worst one are still here.
    const auto [slowest_sql, slowest_limit] = query.slowest_args();
    std::cout << "worst query: " << slowest_sql << " (limit " << slowest_limit << ")\n";
}
```

`snapshot()` returns the rest — call count, total, min and max duration, the
fastest call's arguments, the file and line of the last call — all read under one
lock, so the numbers describe the same moment.

Point tempo at a function or method pointer and you also get a type that knows
the signature — return type, parameter types, arity, whether it is a member,
whether it is const — along with call counts and timings. The wrapper returns the
call expression directly rather than through a named local, so the return value
is never copied or moved and even immovable return types pass through.

When there is no callable to point at, `TEMPO_SCOPE()` times the block it stands
in — one line inside a body you already have, no wrapper object and no call site
touched. It reports into the same table as everything else.

MIT licensed. Header-only, nothing to link. Requires C++20 (concepts,
`std::source_location`, `__VA_OPT__`, `std::erase_if`, `<ranges>`).

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

`/Zc:preprocessor` is **required** on MSVC if you use `TEMPO_PROFILE_CALL`: it
uses `__VA_OPT__`, which the traditional MSVC preprocessor does not implement.
`std::source_location` needs VS 2019 16.10 or newer.

`-pthread` is required wherever your standard library needs it for `<mutex>` and
`<thread>`; on glibc 2.34 and later it links without, but pass it anyway.

### Version

The header defines its own version, so code that vendors a copy can pin the
revision it was written against:

```cpp
#if !defined(TEMPO_VERSION) || TEMPO_VERSION < 100
#error "this code needs tempo 0.1.0 or newer"
#endif
```

`TEMPO_VERSION` is `major * 10000 + minor * 100 + patch`, which orders correctly
across all three fields; `TEMPO_VERSION_MAJOR`, `_MINOR`, `_PATCH` and
`TEMPO_VERSION_STRING` are there too. The current version is `0.1.0`, and the
major number stays at `0` while the API is still free to change.

### Compiler flags

No compiler flag switches anything tempo does — the three macros below are the
only switches it has. What the flags people ask about were checked to do:

| Flag | Effect |
|---|---|
| `-O0` vs `-O2` | The one flag with a measurable effect, and it is on cost rather than behaviour: per-call overhead roughly doubles unoptimized — **≈76 ns** at `-O0` against **≈38 ns** at `-O2`, timing 200k calls of a two-`int` function against the same loop calling it directly (GCC 15, x86-64). Absolute numbers are yours to measure; every one quoted in this README is `-O2`. |
| `-fno-exceptions` | Builds and works — the examples compile clean under it. The guards that skip recording for a throwing call become dead code, since nothing can throw. |
| `-fno-rtti` | Nothing to switch off: tempo reads `__PRETTY_FUNCTION__`, never `typeid`. |
| `-flto` | No effect on correctness. |
| `-std=c++23` | Builds and passes the suite. C++20 is the floor, not a ceiling. |

### The three macros are the switches, and they are ODR-sensitive

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
spelling is not standardised. A free function prints as `tempo::Callable<f>`
under GCC and `tempo::Callable<&f>` under Clang; one in an anonymous namespace
becomes `{anonymous}::f` and `&(anonymous namespace)::f` respectively, and the
name column is capped at 60 characters, so the longer Clang spelling may be
truncated. A bare `TEMPO_SCOPE()` is named from
`std::source_location::function_name()`, which is unstandardised in the same way:
`void {anonymous}::load()` under GCC, `void (anonymous namespace)::load()` under
Clang. `TEMPO_SCOPE_NAMED` gives the same name everywhere. Do not parse the
report; read `snapshot()` or `collect()` instead.

### What CI covers

GCC and Clang on Linux. The test suite runs under the macro combinations that
change behaviour at runtime — per-call printing on and off, recursion counting on
and off — and the examples are compiled under those plus `TEMPO_ENABLED=0`, which
the tests cannot use because they deliberately inspect wrapper types that stop
existing when the switch is off. On top of that: the compile-failure suite under
both compilers, AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer
under Clang over both the tests and the examples, and the suite at C++20 and
C++23.

**Linux is the only platform covered.** macOS and MSVC are not: the code paths
for both exist and are written against the documented behaviour, and the macOS
build is expected to work, but nothing verifies either. The macOS job is still
in `.github/workflows/ci.yml`, commented out, ready to be switched back on.

## Instrumenting without touching call sites

The wrapper object above still has to be named at each call site.
`TEMPO_INSTRUMENT` is the alternative: name the function once, at its
declaration, and leave every call site alone. A variable and a function cannot
share a name in one scope but they can across scopes, so the function lives in a
nested namespace and the wrapper takes its name outside:

```cpp
namespace impl {
    std::size_t render_page(const std::string& path, int width);
}

TEMPO_INSTRUMENT(impl::render_page, render_page);

render_page("/index.html", 1280);        // every existing call site, unchanged

const auto stats = render_page.snapshot();
```

The call site is still recorded, and no macro is involved in making the call.
`Metrics::operator()` is declared with the callable's exact parameter list plus a
trailing `std::source_location` that defaults to `current()` — and because those
parameters come from the class template rather than being deduced from the call,
the default argument is evaluated at the caller.

Define `TEMPO_ENABLED` as `0` on the command line and every instrumented name
collapses to a plain function pointer that the optimizer inlines away, so the
same source builds with tempo entirely absent.

Member functions cannot use the seam — `service.handle(...)` offers no free name
for a wrapper to shadow — so the instance is passed as the first argument. That
one parameter is deduced, so `ClassName&`, `ClassName*`, `reference_wrapper` and
smart pointers all still bind, while the method's own parameters stay fixed and
the location keeps defaulting at the caller:

```cpp
tempo::CallableMetrics<&Service::handle> handle;
handle(service, 42);        // object first, then the method's arguments
handle(&service, 42);       // pointer, reference_wrapper and smart pointers too
```

A deduced *pack* could not do this — it would swallow the trailing location
argument — but a single deduced parameter followed by fixed ones can, which is
why member calls need no macro either.

### `Metrics` needs no macro; `Profiler` still does

| | plain call |
|---|---|
| `Metrics` over a free function | captures the caller |
| `Metrics` over a lambda or functor | captures the caller |
| `Metrics` over a member function | captures the caller |
| `Profiler` (any callable) | location is `tempo.hpp`; use `TEMPO_PROFILE_CALL` |

`Profiler` is the exception: its `operator()` is variadic for every callable kind
and calls `source_location::current()` in its own body, so the location it
records is the header's, not yours. Counting is unaffected — only the reported
call site is. Use `TEMPO_PROFILE_CALL` whenever the location matters, or
`tempo::measure` instead, which does not need it.

## Timing a block: `TEMPO_SCOPE`

Everything above wraps something *callable*. `TEMPO_SCOPE` goes the other way and
times the braces it stands in — one line inside a body you already have, no
wrapper object, no call site edited, nothing moved into another namespace:

```cpp
void render_frame() {
    TEMPO_SCOPE();                       // row is named after the function
    ...
}                                        // the destructor stops the clock
```

It is the only form that reaches code with no callable to point at: a
constructor or destructor body, a virtual override called through a base
pointer, one branch, one loop body, half of a function, `main` itself.

**Use `TEMPO_SCOPE_NAMED` for two scopes in the same function.** Both are
measured correctly either way — each expansion gets its own statistics — but
`TEMPO_SCOPE()` names its row after the *enclosing function*, so two of them
produce two rows carrying the same label and nothing in the report tells them
apart:

```cpp
void parse_and_write(const std::vector<std::string>& lines) {
    { TEMPO_SCOPE_NAMED("parse"); for (const auto& line : lines) parse(line); }
    { TEMPO_SCOPE_NAMED("write"); flush(); }
}
```

### The destructor is the mechanism, and the statistics outlive the object

The object on the stack holds only the start time; the counters are static
members selected at compile time. So in a loop body, where the timer is
constructed and destroyed on every pass, you get **one sample per iteration** —
`avg` is the cost of one iteration and `max` is the worst single one:

```cpp
for (const Job& job : jobs) {
    TEMPO_SCOPE_NAMED("job");            // 1 sample per iteration
    process(job);
}

{
    TEMPO_SCOPE_NAMED("all jobs");       // 1 sample for the whole loop
    for (const Job& job : jobs) { process(job); }
}
```

Both forms can coexist. Scopes are independent: an enclosing scope never
suppresses an inner one. `break`, `continue` and `return` all record, because
they are scope exits. A scope left by a *throw* is counted as entered but is not
timed — the region never finished. A scope re-entered by recursion counts every
entry and times only the outermost, so the total is wall clock rather than a sum
of intervals that contain one another, and the report grows a depth column.

Each expansion needs its own statistics, and the tag that provides them is
`decltype([]{})`: every lambda *expression* has a distinct closure type, so every
expansion names a distinct instantiation. The counters are then ordinary statics
resolved at compile time — no runtime lookup and no hashing of a name on the way
in. One consequence is worth knowing: a `TEMPO_SCOPE` in a function *template*
gets one row per instantiation, while one in an inline function in a header gets
a single row no matter how many translation units include it.

Because it goes through a macro, `-DTEMPO_ENABLED=0` removes it completely: no
object, no statics, the block left exactly as written.

### What it cannot do

No arguments and no return value — a block has neither, so there is no scope
equivalent of `slowest_args()`. And it counts *block entries*, which equals a
call count only when the scope is the first statement of a function.

Overhead is two clock readings, an atomic increment and one mutex acquire per
entry — around 30 ns here. Fine for a block that takes microseconds, and
dominant in a tight loop over a few nanoseconds of work; time the loop from
outside and divide instead.

## Lambdas and functors

Lambdas and functors are objects, not pointers, so they cannot be template
arguments. Three factories take the object instead: `tempo::wrap` for counting,
`tempo::profile` for counting plus call-site reporting, and `tempo::measure` for
the full set — counting, timing and the fastest and slowest arguments.

```cpp
auto parse = tempo::measure([](std::string_view line) { return parse_row(line); });

for (const auto& line : lines) { parse(line); }

const auto [worst_line] = parse.slowest_args();   // the row that took longest
```

A closure has a concrete signature, so the wrapper's `operator()` is built from
it and the call site is captured with no macro. A *generic* lambda
(`[](auto x){}`) has no signature until it is called and is rejected outright.

## Recursion

A recursive call is resolved at compile time to the function itself and never
touches the pointer the wrapper holds, so no library can intercept it — the body
has to name the wrapper. `TEMPO_RECURSIVE` sets that up in one line:

```cpp
TEMPO_RECURSIVE(std::size_t, count_nodes, const Node& node) {
    std::size_t total = 1;
    for (const Node* child : node.children) { total += TEMPO_SELF(count_nodes)(*child); }
    return total;
}

count_nodes(root);   // an ordinary call site, as with TEMPO_INSTRUMENT

count_nodes.snapshot().max_depth;   // how deep the tree actually went
```

The macro declares the real function under a suffixed name — `TEMPO_TARGET(count_nodes)`,
which expands to `count_nodes_tempo_target` — points a wrapper at it under the plain
name, and then opens the real definition, which is why the body you write follows
the macro directly. You never need to write `TEMPO_TARGET` yourself; it is the
name `TEMPO_SELF` resolves to when recursion counting is off.

`TEMPO_SELF` is the whole switch. With `TEMPO_COUNT_RECURSION` at its default of
`0` it names the real function, so recursion costs nothing and only the outermost
call is counted — exactly what an untouched recursive function does. Set it to
`1` and it names the wrapper instead, and every level is counted:

| | calls | deepest | total time |
|---|---|---|---|
| `TEMPO_COUNT_RECURSION=0` | 1 | 1 | 0.08 ms |
| `TEMPO_COUNT_RECURSION=1` | 150049 | 24 | 1.9 ms |

Measured by `examples/06_recursion.cpp` on `fibonacci(24)`, GCC `-O2`.

Off is the default because counting is not free: routing 150049 recursive calls
through the wrapper turned a 0.08 ms computation into 1.9 ms, about 12 ns each.
That is cheaper than a fully measured call, since an inner call takes no clock
reading and never touches the statistics lock — it still pays for the counter,
the depth bookkeeping and a copy of its arguments. Use it to answer "how many
times does this really run", not to time a hot recursion.

Timing stays correct in both modes because only the outermost call is timed.
Timing every level would sum intervals that contain one another and report a
multiple of the time the program actually spent. For the same reason
`average_ms()` divides by `timed_calls`, the outermost count, not by every
recursive step. `max_depth` records the deepest level reached, and the report
grows a `depth` column only when something actually recursed, so an ordinary
program prints the table it always did.

Depth is tracked per thread, so threads recursing independently do not disturb
each other, and it is restored correctly when a call throws. Only direct
recursion is covered; mutual recursion needs both functions instrumented, though
the timing is right either way. A return type containing a comma has to go behind
a type alias first — the preprocessor would split it.

`TEMPO_INSTRUMENT` is unaffected: it never routes recursion through the wrapper,
so an existing recursive function keeps measuring its base call only.

## Counting constructions

`ConstructorProfiler` counts how many objects of a type were built through it. It
forwards its arguments straight into the constructor and returns the object as a
prvalue, so it is built in the caller's storage — no temporary, no copy, no move,
and it works for types that can do neither:

```cpp
tempo::ConstructorProfiler<Connection> make_connection;

Connection c = make_connection("db-1", 5432);

make_connection.obj_count;                              // objects built here
static_assert(make_connection.can_construct<const char*, int>);
```

It sees only the constructions that go through it: a plain `Connection c{...}`
elsewhere is invisible, as are copies, destructions and constructors that throw.

## Reporting

tempo is quiet by default. Statistics are collected on every call and read back
through the accessors, or all at once as one sorted summary from
`tempo::report::print()`:

```
=== tempo report ===================================================
callable                     calls    total ms      avg ms      min ms      max ms
----------------------------------------------------------------------------------
tempo::Callable<worker>       2000      2.3531      0.0012      0.0002      0.0328
tempo::Callable<slow_path>       5      0.1263      0.0253      0.0092      0.0441
tempo::Callable<fast_path>      50      0.0011      0.0000      0.0000      0.0001
==================================================================================
```

Rows are sorted by total time, so the hot spot is the first line, and a metric
that was never called is left out. Every metric registers itself on its first
call, `TEMPO_SCOPE` blocks included — they share the table with the wrappers.
`tempo::report::at_exit()` prints the table when the program ends, and
`tempo::report::reset_all()` clears everything. Read statistics with
`snapshot()`, which returns them all under one lock so the numbers describe the
same moment.

`tempo::report::collect()` returns the same rows as a `std::vector<Row>` instead
of a table — including the ones that were never called — for when you want the
numbers rather than the layout. It is the only way to read a `TEMPO_SCOPE`, whose
tag type has no name you can write.

Define `TEMPO_PRINT_ENABLED` as `1` to get a block of lines on every call
instead — the clearer view when you are watching a handful of calls, and unusable
on anything called often. It is off by default because the printing happens under
the same lock as the recording.

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
| ref-qualified or variadic `operator()` | same, for a functor's call operator |
| lambda passed to the macros | points at `tempo::measure` instead |
| generic lambda, or overloaded `operator()` | explains that it has no signature until called |
| a type that is not callable at all | says what a callable object has to look like |
| `tempo::Metrics<MyLambda>` | names the wrapper type that was meant |
| wrong arguments at a call site | reminds you the instance comes first for a method |
| `slowest_args()` with unstorable args | explains which parameter disabled capture |
| `ConstructorProfiler<int>` | says it needs a class |

The one-error guarantee is enforced, not hoped for: `tests/errors` compiles
each mistake and fails if it produces more than one error, or an error that does
not carry tempo's own wording. Both compilers run it in CI, because they do not
agree on which unsupported shapes they will silently accept.

## noexcept

A `noexcept` callable gets a `noexcept` wrapper. The qualifier is read off the
callable and reapplied to every call operator on the way out, so an instrumented
name still satisfies `noexcept(f(x))` and nothing that depended on the guarantee
changes meaning:

```cpp
namespace impl { int clamp_volume(int level) noexcept; }
TEMPO_INSTRUMENT(impl::clamp_volume, clamp_volume);

static_assert(noexcept(clamp_volume(1)));   // still true through the wrapper
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
| `09_scope.cpp` | timing a block, a loop body and a whole loop |

`make matrix` compiles them all under every macro combination, `make sanitize`
runs them under address and undefined behaviour, `make tsan` under the thread
sanitizer.

## Tests

```
cd tests && make run          # the whole suite
make matrix                   # every combination of the macros
make errors                   # the compile-failure suite: one clear message each
make sanitize                 # address + undefined behaviour
make tsan                     # data races
```

Each file is a separate binary, like the examples, and exits non-zero on
failure. 137 tests, 447 checks.

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
| `12_scope.cpp` | `TEMPO_SCOPE`: per-iteration samples, recursion, throws, threads |
| `errors/` | 14 mistakes that must NOT compile, each with one clear message |

## Known limits

**Callables tempo will not read.** C-style variadic functions, generic lambdas
(`[](auto x){}`), overloaded `operator()`, ref-qualified or `volatile` call
operators and member functions, and overloaded function names. Every one of them
is diagnosed with a sentence rather than left to the compiler.

**Statistics are per wrapped type, not per object.** Counters, timings and
captured arguments are static members of the wrapper type, so two wrapper objects
over the same function share one set of numbers — and every
`std::function<int(int)>` in a program is the same type. Wrap the underlying
lambda instead.

**Argument capture needs copyable, default-constructible parameters.** A
move-only parameter such as `std::unique_ptr`, a reference member, or a type with
no default constructor sets `tracks_args` to `false`, and `slowest_args()` then
becomes a compile error naming the parameter responsible. `noexcept` callables
are stricter still, as described above. Timing, counts and the report are
unaffected.

**Short calls mostly measure tempo.** A wrapped call that returns immediately
reports about 15 ns here, which is essentially the two clock readings around it.
Measure work that takes longer than the instrument. The same applies to
`TEMPO_SCOPE` in a tight loop: at roughly 30 ns per entry it will dominate a body
of a few nanoseconds, so time the loop from outside and divide instead.

**A scope has no arguments and no return value.** `TEMPO_SCOPE` measures a region
of code, not a call, so there is no scope equivalent of `slowest_args()` — the
thing tempo is otherwise for. It also counts block *entries*, which equal a call
count only when the scope is the first statement of a function. And its tag type
has no name you can write, so its numbers are readable only through
`tempo::report::collect()` or the printed table, never through a `snapshot()` you
call yourself.

**`TEMPO_ENABLED=0` only neutralises the macros.** Names introduced by
`TEMPO_INSTRUMENT` and `TEMPO_RECURSIVE` collapse to plain function pointers; a
`tempo::CallableMetrics<&f>` object written out by hand is still a metric and
still measures. Anything that has to compile away must go through the macros.

**Recursion is direct-only.** Mutual recursion needs both functions instrumented,
`TEMPO_RECURSIVE` has no slot for `noexcept`, and a return type containing a
comma has to go behind a type alias first.

**Members change their call sites.** A method has no free name for a wrapper to
shadow, so a wrapped one is called as `m(object, args...)` — there is no
equivalent of the `TEMPO_INSTRUMENT` seam for it. And `Profiler` needs
`TEMPO_PROFILE_CALL` to report your call site instead of the header's; `Metrics`
does not.

**No percentiles, no histograms, no call graph.** Samples are not retained, so
the summary has totals and extremes and nothing else, and only what you name is
measured — nothing is discovered for you, inlined callees included. Scope rows are
inclusive time per site, not a tree: a scope containing a call into another
scoped function contributes to both rows, since the depth gate only suppresses
re-entry of the *same* scope. tempo answers "which input was slow", not "where is
the time going" — that is a sampling profiler's job, and the two go together well.

**Linux, GCC and Clang are what is verified.** macOS and MSVC have code paths and
no CI behind them.
