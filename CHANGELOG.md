# Changelog

Notable changes to tempo. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and tempo follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html) -- with the usual
0.x caveat that the API may change between minor versions until 1.0.

## [Unreleased]

Nothing yet.

## [0.1.0] - 2026-09-16

First release. tempo is a single header with no dependencies: put `tempo.hpp`
on the include path, compile as C++20, and there is nothing to link.

### Measuring

- `tempo::CallableMetrics<&function>` and `tempo::CallableMetrics<&Type::method>`
  wrap a function or member function. A member is called through the metric as
  `metric(object, args...)`, with the instance accepted as a reference, a
  pointer, a `std::reference_wrapper` or a smart pointer.
- `tempo::measure(callable)` does the same for lambdas, functors and
  `std::function`, with `tempo::wrap` and `tempo::profile` for the counting-only
  and call-site-only cases.
- `TEMPO_INSTRUMENT(impl::function, function)` instruments an existing free
  function without touching its call sites.
- `TEMPO_SCOPE()` and `TEMPO_SCOPE_NAMED("name")` time a region of code where
  there is no callable to wrap -- a branch, a loop, part of a function.
- `TEMPO_RECURSIVE` declares a recursive function whose nested entries are not
  double-counted.
- `tempo::ConstructorProfiler<Type>` counts constructions made through it.

### Reading the results

- `snapshot()` returns, under one lock, the call count, total, minimum and
  maximum duration, the fastest and slowest arguments, and the last call site.
- `worst_calls()` returns the slowest completed calls, slowest first, each
  carrying its duration, its arguments and the `std::source_location` of the
  call that produced it. The capacity defaults to ten and is a template
  argument: `tempo::CallableMetrics<&run_query, 25>`, `tempo::measure<25>(...)`.
- `tempo::report::collect()` returns every registered metric as structured
  rows; `tempo::report::print()` writes a table sorted by total time;
  `reset_all()` clears every metric.
- Arguments are captured only when every parameter type can be stored. A
  move-only parameter turns capture off and leaves timing and counting intact;
  `tracks_args` says which applies.

### Configuration

Compile definitions, to be set consistently across every translation unit:
`TEMPO_ENABLED`, `TEMPO_WORST_CALLS`, `TEMPO_COUNT_RECURSION`,
`TEMPO_PRINT_ENABLED`. Version macros are `TEMPO_VERSION_MAJOR` / `_MINOR` /
`_PATCH`, `TEMPO_VERSION` and `TEMPO_VERSION_STRING`.

### Rejecting what it cannot measure faithfully

Generic lambdas, overloaded call operators, C-style variadics, and `volatile`
or ref-qualified members are rejected at compile time with a single readable
message that names the fix, rather than a page of template instantiation
noise. Fifteen intentional compile failures in the test suite hold that
property in place.

### Building and packaging

- CMake package exporting the header-only target `tempo::tempo`, installable
  and consumable through `find_package(tempo CONFIG REQUIRED)`. The target
  supplies `/Zc:preprocessor` on MSVC, which tempo's variadic macros require
  and which `/std:c++20` does not enable by itself.
- Verified on GCC and Clang on Linux, Apple Clang on macOS and MSVC on
  Windows, across C++20 and C++23, under AddressSanitizer, UndefinedBehavior
  and ThreadSanitizer, with an installed-package consumer test and the macro
  configuration matrix. The suite is 159 runtime tests and 538 checks.

### Known limits

Statistics live on the wrapper type rather than the wrapper object, so one
function cannot be measured in two contexts independently and there is no
per-instance metric. A wrapped call costs roughly 37 ns, which makes tempo the
wrong tool for work shorter than a few hundred nanoseconds. There are no
percentiles, histograms, call trees, or automatic discovery of hot code. See
[Important limits](README.md#important-limits) for the full list.

[Unreleased]: https://github.com/oktayelb/tempo/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/oktayelb/tempo/releases/tag/v0.1.0
