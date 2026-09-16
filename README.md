# tempo

**Input-aware C++20 instrumentation.**

Most profilers tell you which function was slow. tempo also keeps the arguments
and call site behind its slowest calls, so you can see which input caused the
problem.

It is a header-only library with no dependencies. It measures selected
functions or scopes; it is not a sampling profiler and does not build a call
graph.

## Quick start

```cpp
#include "tempo.hpp"

std::size_t run_query(const std::string& sql, int limit);

tempo::CallableMetrics<&run_query> query;

void nightly_job() {
    query("select id from orders where status = 'open'", 100);
    query("select * from orders join items using (order_id)", 5000);

    const auto& [sql, limit] = query.slowest_args();
    std::cout << "worst query: " << sql << " (limit " << limit << ")\n";
}
```

`query.snapshot()` returns call count, total/min/max duration, fastest and
slowest arguments, and the last call site under one lock. By default, tempo
also retains the ten slowest completed calls. Read them with `worst_calls()`,
slowest first:

```cpp
for (const auto& call : query.worst_calls()) {
    const auto& [sql, limit] = call.args;
    std::cout << call.duration.count() << " ms  limit=" << limit
              << "  " << sql << '\n';
}
```

## What it measures

| Use case | API |
|---|---|
| Free function | `tempo::CallableMetrics<&function> metric;` |
| Member function | `tempo::CallableMetrics<&Type::method> metric; metric(object, args...);` |
| Lambda, functor, or `std::function` | `auto metric = tempo::measure(callable);` |
| Instrument a free function without changing call sites | `TEMPO_INSTRUMENT(impl::function, function);` |
| Time a region of code | `TEMPO_SCOPE()` or `TEMPO_SCOPE_NAMED("name")` |
| Count constructions made through a factory | `tempo::ConstructorProfiler<Type>` |

All timed metrics provide counts and total, average, minimum, and maximum
duration. `tempo::report::collect()` returns every metric as structured rows;
`tempo::report::print()` writes a summary sorted by total time.

### Instrument an existing free function

Put the implementation in a nested namespace and expose the wrapper under the
old name. Existing call sites keep calling `render_page(...)` normally.

```cpp
namespace impl {
    std::size_t render_page(const std::string& path, int width);
}

TEMPO_INSTRUMENT(impl::render_page, render_page);

render_page("/index.html", 1280);
const auto stats = render_page.snapshot();
```

### Time a scope

Use a scope when there is no callable to wrap: a branch, loop, constructor,
destructor, virtual override, or part of a function.

```cpp
void render_frame() {
    TEMPO_SCOPE();
    // work to measure
}

void parse_and_write() {
    { TEMPO_SCOPE_NAMED("parse"); /* parse */ }
    { TEMPO_SCOPE_NAMED("write"); /* write */ }
}
```

Scope metrics are available through `tempo::report::collect()` and the printed
report. They record entries and durations, but have no arguments or return
value.

## Build

Copy `tempo.hpp` onto your include path and compile your program as C++20.
There is nothing to link or configure.

```sh
# GCC or Clang
g++ -std=c++20 -O2 -pthread -I/path/to/tempo your.cpp -o your_program

# MSVC
cl /std:c++20 /Zc:preprocessor /EHsc /O2 /I path\to\tempo your.cpp
```

`/Zc:preprocessor` is required on MSVC when using tempo's variadic macros;
`/std:c++20` does not enable it on its own. The exported `tempo::tempo` target
adds the flag for you, so this only matters when invoking `cl` by hand.
tempo requires C++20, including `std::source_location`, concepts, and ranges.

### CMake

tempo exports the header-only target `tempo::tempo` when installed:

```cmake
find_package(tempo CONFIG REQUIRED)
target_link_libraries(your_program PRIVATE tempo::tempo)
```

To build, test, and install tempo itself:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/install/prefix
```

## Configuration

Set these compile definitions consistently for every translation unit. They
change inline/template bodies; mixing values in one program is an ODR violation.

| Definition | Default | Effect |
|---|---:|---|
| `TEMPO_ENABLED` | `1` | Makes `TEMPO_INSTRUMENT`, `TEMPO_RECURSIVE`, and scope macros compile away when `0`. Explicit `CallableMetrics` objects still measure. |
| `TEMPO_WORST_CALLS` | `10` | Default number of retained slow calls. `0` disables ranking but keeps one fastest and slowest call. |
| `TEMPO_COUNT_RECURSION` | `0` | Counts recursive entries. Timing remains outermost-only to avoid double-counting nested time. |
| `TEMPO_PRINT_ENABLED` | `0` | Prints individual measurements; useful for a few calls, unsuitable for hot paths. |

Choose a per-metric ranking capacity when needed:

```cpp
tempo::CallableMetrics<&run_query, 25> query;
auto parse = tempo::measure<25>([](std::string_view line) { /* ... */ });
```

## Important limits

- Measure work that is longer than the instrumentation. A wrapped call costs
  about 37 ns (x86-64 Linux, GCC `-O2`) -- two clock reads and a mutex -- so
  work under roughly 370 ns is distorted by more than 10%. The ranking is not
  what costs: `TEMPO_WORST_CALLS=0` measures the same, so turning it off buys
  nothing.
- The clock's own resolution is a second floor, and a coarser one on Windows.
  `std::chrono::steady_clock` resolves to roughly 100 ns there, so a call
  shorter than one tick is timed as exactly zero. That is a correct measurement
  of work too short to measure, not a bug -- but it means `total_duration` can
  read `0.0` after several completed calls. Time a loop of such calls, or put a
  scope around them, rather than instrumenting each one.
- Argument capture stores decayed copies for the life of the metric. Do not
  capture sensitive or very large values casually; move-only or otherwise
  unstorable parameters disable argument capture but still allow timing.
- Statistics live on the wrapper **type**, not on the wrapper object. Every
  `CallableMetrics<&f>` in a program shares one set of counters, so the same
  function cannot be measured in two contexts independently and there is no
  per-instance or per-request metric. This is what lets `TEMPO_INSTRUMENT`
  stand in for a function with no storage at the call site, and it is the main
  thing to know before designing around tempo. Distinct
  `std::function<int(int)>` objects share one metric for the same reason; wrap
  their underlying lambdas instead.
- Supported callables have a concrete, non-overloaded signature. Generic
  lambdas, overloaded call operators/functions, C-style variadics, and
  `volatile` or ref-qualified members are rejected with a diagnostic.
- Member functions are called through the metric as `metric(object, args...)`.
- There are no percentiles, histograms, call trees, or automatic discovery of
  hot code. Pair tempo with a sampling profiler when you need those answers.

## Status and verification

tempo is **0.1.0**: the API is still allowed to change. CI is configured for
GCC and Clang on Linux, Apple Clang on macOS, and MSVC on Windows. It also runs
examples, C++20/C++23 builds, sanitizer jobs, an installed-package consumer
test, and compile-failure checks on the direct-compiler lanes.

The current suite contains **159 runtime tests and 538 checks**, plus **15**
intentional compile failures that must emit one readable tempo diagnostic.
Release-to-release changes are recorded in the [changelog](CHANGELOG.md).

```sh
cd tests
make run       # runtime suite
make errors    # compile-failure diagnostics
make matrix    # local macro combinations
make sanitize  # AddressSanitizer + UBSan
make tsan      # ThreadSanitizer

cd ../examples
make run
```

The [examples](examples/) show one focused use case each, including slow-call
ranking, scopes, recursion, constructor counting, and reporting.

## License

[MIT](LICENSE).
