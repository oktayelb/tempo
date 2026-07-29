#pragma once

// tempo — Copyright (c) 2026 Oktay Elibüyük
// Released under the MIT License. See LICENSE for the full terms.

// tempo's version, as three numbers and as one comparable integer. The single
// integer is the one to test against, since it orders correctly across all
// three fields:
//
//     #if !defined(TEMPO_VERSION) || TEMPO_VERSION < 10000
//     #error "this code needs tempo 1.0.0 or newer"
//     #endif
//
// Major stays at 0 while the API is still free to change; a program that
// vendors the header can pin the exact revision it was written against.
#define TEMPO_VERSION_MAJOR 0
#define TEMPO_VERSION_MINOR 1
#define TEMPO_VERSION_PATCH 0
#define TEMPO_VERSION \
    (TEMPO_VERSION_MAJOR * 10000 + TEMPO_VERSION_MINOR * 100 + TEMPO_VERSION_PATCH)
#define TEMPO_VERSION_STRING "0.1.0"

#if defined(_MSC_VER)
#if !defined(_MSVC_LANG) || _MSVC_LANG < 202002L
#error "tempo requires C++20 support"
#endif
#elif __cplusplus < 202002L
#error "tempo requires C++20 support"
#endif

#include <iostream>
#include <type_traits>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <concepts>
#include <exception>
#include <functional>
#include <iomanip>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <source_location>
#include <utility>
#include <vector>

// Print a block of lines on every single call? Off by default: a profiler that
// writes eight lines to cout per call -- while holding the lock, at that -- is
// unusable on anything called often, and the aggregated tempo::report() says the
// same things better. Statistics are collected either way; this switch only
// decides whether each call narrates itself.
//
// Set it to 1 to get the per-call trace back, which is genuinely the clearer
// view when you are watching a handful of calls. Define it BEFORE the include.
#ifndef TEMPO_PRINT_ENABLED
#define TEMPO_PRINT_ENABLED 0
#endif

// Master switch for TEMPO_INSTRUMENT. Define it as 0 in release builds and every
// instrumented name collapses to a plain function pointer, which the optimizer
// inlines away to nothing. Define it BEFORE the include.
#ifndef TEMPO_ENABLED
#define TEMPO_ENABLED 1
#endif

// Instrument a function once, at its declaration, and leave every call site
// alone.
//
//     namespace impl { int fibonacci(unsigned n); }
//     TEMPO_INSTRUMENT(impl::fibonacci, fibonacci);
//
//     fibonacci(26);   // unchanged: resolves to the wrapper's operator()
//
// A variable and a function cannot share a name in one scope, but they can
// across scopes -- so the function lives in a nested namespace and the wrapper
// takes its name outside. Because the wrapper's operator() carries a defaulted
// source_location (see detail::FixedSignatureCall), the call site is still
// recorded even though nothing at the call site changed.
//
// Member functions cannot use this: service.handle(...) offers no free name for
// a wrapper to shadow. Use TEMPO_CALLABLE_METRICS and an explicit call for those.
#if TEMPO_ENABLED
#define TEMPO_INSTRUMENT(function, alias) inline ::tempo::CallableMetrics<&function> alias{}
#else
#define TEMPO_INSTRUMENT(function, alias) inline constexpr auto alias = &function
#endif

// Should recursive calls be measured too? Off by default, and deliberately so: a
// recursive call that goes through the wrapper pays the full per-call cost, which
// on a hot recursion is a large multiple of the work itself. Leave it off to
// measure the top-level call; switch it on to answer "how many times does this
// actually run". Define it BEFORE the include.
#ifndef TEMPO_COUNT_RECURSION
#define TEMPO_COUNT_RECURSION 0
#endif

// The real function behind a TEMPO_RECURSIVE definition. The wrapper takes the
// plain name, so the function itself needs a different one.
#define TEMPO_TARGET(name) name##_tempo_target

// Define a recursive function and instrument it in one go:
//
//     TEMPO_RECURSIVE(int, fibonacci, unsigned n) {
//         return n < 2 ? n : TEMPO_SELF(fibonacci)(n-1) + TEMPO_SELF(fibonacci)(n-2);
//     }
//
//     fibonacci(26);   // call sites are ordinary, exactly as with TEMPO_INSTRUMENT
//
// The macro declares the real function under a suffixed name, points a wrapper
// at it under the plain name, and then opens the real definition -- so the body
// you write follows the macro directly. TEMPO_SELF picks which of the two a
// recursive call reaches, and that is the whole switch:
//
//   TEMPO_COUNT_RECURSION=0  -> the real function: no wrapper, no cost, and only
//                               the outermost call is counted, exactly as if the
//                               body had simply called itself.
//   TEMPO_COUNT_RECURSION=1  -> the wrapper: every recursive call is counted.
//
// Timing stays correct either way, because Metrics times only the outermost call
// (see the depth gate in RecordOnExit). A return type containing a comma has to
// be hidden behind a type alias first -- the preprocessor would split it.
#define TEMPO_RECURSIVE(returns, name, ...)                 \
    inline returns TEMPO_TARGET(name)(__VA_ARGS__);         \
    TEMPO_INSTRUMENT(TEMPO_TARGET(name), name);             \
    inline returns TEMPO_TARGET(name)(__VA_ARGS__)

#if TEMPO_COUNT_RECURSION
#define TEMPO_SELF(name) name
#else
#define TEMPO_SELF(name) TEMPO_TARGET(name)
#endif

#define TEMPO_CALLABLE(callable) ::tempo::Callable<&callable>
#define TEMPO_FUNCTION(function) ::tempo::Function<&function>
#define TEMPO_METHOD(method) ::tempo::Method<&method>
#define TEMPO_CALLABLE_PROFILER(callable) ::tempo::CallableProfiler<&callable>
#define TEMPO_CALLABLE_METRICS(callable) ::tempo::CallableMetrics<&callable>
#define TEMPO_PROFILE_CALL(profiler, ...) (profiler).call_at(::std::source_location::current() __VA_OPT__(,) __VA_ARGS__)
#define TEMPO_METRICS_CALL(metrics, ...) (metrics).call_at(::std::source_location::current() __VA_OPT__(,) __VA_ARGS__)

namespace tempo{
//-------------------------------------------------------------------
// The type of every counter tempo keeps: calls, timed calls, constructions.
//
// 64 bits rather than 32 because a counter that wraps is worse than no counter
// at all -- it reports a small number with no indication anything was lost. A
// function called once a microsecond overflows an unsigned int in about 72
// minutes, which is an ordinary lifetime for a server process, and the
// instrumented functions most worth counting are exactly the hot ones. At 64
// bits the same call rate needs half a million years.
//
// Named once and used everywhere so the width cannot drift between the counters
// that hold it, the snapshots that copy it and the report that prints it.
//
// Recursion depth deliberately does NOT use this: it is bounded by the stack,
// cannot approach even 32 bits, and reads better narrow.
using CallCount = std::uint64_t;

template <auto Value>
concept FunctionPointer =
    std::is_pointer_v<decltype(Value)> &&
    std::is_function_v<std::remove_pointer_t<decltype(Value)>>;

template <auto Value>
concept MethodPointer = std::is_member_function_pointer_v<decltype(Value)>;

template <auto Value>
concept SupportedCallable = FunctionPointer<Value> || MethodPointer<Value>;

namespace detail {

// To store arguments we strip the references off the signature:
// std::tuple<const T&> is neither default constructible nor reassignable.
template <typename Tuple>
struct DecayedTuple;

template <typename... Ts>
struct DecayedTuple<std::tuple<Ts...>> {
    using Type = std::tuple<std::decay_t<Ts>...>;
};

// We can only store arguments that are copy constructible and default
// constructible. For move-only arguments (unique_ptr and friends) storage is
// silently switched off.
template <typename Tuple>
struct ArgsAreStorable;

template <typename... Ts>
struct ArgsAreStorable<std::tuple<Ts...>>
    : std::bool_constant<(std::copy_constructible<std::decay_t<Ts>> && ...) &&
                         (std::default_initializable<std::decay_t<Ts>> && ...)> {};

// The same question for a noexcept callable, where the answer has to be
// stronger.
//
// Capturing an argument means copying it, and a copy that allocates can throw --
// inside a wrapper that has just promised its callers it will not. tempo will
// not turn a noexcept function into one that terminates on a full heap, so for
// those callables capture is restricted to argument types it can copy, store and
// overwrite without throwing. Everything else switches capture off, exactly as
// a move-only parameter already does, and says so through tracks_args.
//
// Assignment is in the list because the stored tuple is overwritten in place
// every time a new extreme is seen, not just constructed once.
template <typename Tuple>
struct ArgsAreNothrowStorable;

template <typename... Ts>
struct ArgsAreNothrowStorable<std::tuple<Ts...>>
    : std::bool_constant<
          (std::is_nothrow_default_constructible_v<std::decay_t<Ts>> && ...) &&
          (std::is_nothrow_copy_constructible_v<std::decay_t<Ts>>    && ...) &&
          (std::is_nothrow_copy_assignable_v<std::decay_t<Ts>>       && ...) &&
          (std::is_nothrow_move_assignable_v<std::decay_t<Ts>>       && ...)> {};

// A template-dependent "false", so the static_assert only fires when the
// template is actually instantiated. A plain "false" would make the compiler
// reject it on sight, before any instantiation.
template <typename...>
inline constexpr bool always_false = false;

// The same thing for a non-type template argument, so an assert can depend on a
// function pointer rather than a type.
template <auto...>
inline constexpr bool always_false_value = false;

//-------------------------------------------------------------------
// Keeping a failed assert to ONE message.
//
// Every static_assert below sits in a template that the rest of the header goes
// on to use: Callable inherits from it, Metrics reads its typedefs, the call
// operator forwards to it. If the failing template were left empty, the assert
// would be followed by "incomplete type", "no member named ReturnType", "no
// member named operator()" and so on -- exactly the wall of errors these asserts
// exist to remove.
//
// So each failing template inherits UnsupportedCallable instead. It provides
// every member the downstream machinery looks for, which keeps the compiler
// quiet after the one message we actually wrote. None of it ever runs: the
// program does not compile.
//
// A parameter type that is deliberately not default-constructible, so
// ArgsAreStorable is false for the stand-in and argument capture switches itself
// off rather than trying to store this type.
struct UnsupportedArg {
    UnsupportedArg() = delete;
};

// A return type that converts both to and from anything.
//
// The failing wrapper still has to return SOMETHING, and whatever the user wrote
// at the call site has to keep type-checking -- otherwise "int x = f(1);" adds
// "cannot convert" and "return f(1);" adds "void value not ignored as it ought
// to be", both underneath the message we actually wanted them to read.
struct UnsupportedReturn {
    UnsupportedReturn() = default;

    // From anything. Constrained away from UnsupportedReturn itself so that it
    // does not hide the copy constructor.
    template <typename T>
        requires (!std::same_as<std::decay_t<T>, UnsupportedReturn>)
    UnsupportedReturn(T&&) {}

    // To anything. Declared and never defined on purpose: it exists only to
    // satisfy the type checker, and a translation unit that instantiates it has
    // already failed, so it never reaches the linker.
    template <typename T>
    operator T() const;
};

struct UnsupportedCallable {
    using ReturnType = UnsupportedReturn;
    using ArgsType   = std::tuple<UnsupportedArg>;
    using ClassType  = void;

    // is_member is true so that Metrics routes to the unconstrained variadic
    // call operator below rather than to a fixed signature. A fixed signature
    // would reject the user's actual call arguments and add a second error on
    // top of the one we are trying to deliver.
    static constexpr bool is_member = true;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = false;
    static constexpr bool is_const_callable = true;
    // False so the stand-in's call operators stay unqualified. A noexcept
    // wrapper around a callable that does not exist would only add "call to
    // non-noexcept function" underneath the message we are trying to deliver.
    static constexpr bool is_noexcept = false;
    static constexpr std::size_t arg_count = 0;
    static constexpr std::size_t total_arg_size = 0;

    inline static std::atomic<CallCount> call_count{0};

    // Accepts anything and returns something that converts to anything, so no
    // call site produces a follow-up error.
    template <typename... CallArgs>
    UnsupportedReturn operator()(CallArgs&&...) const { return {}; }
};

// The two qualifiers that used to need detecting by hand, and no longer do.
//
// noexcept is now SUPPORTED: the qualifier is read off the callable and
// reapplied to every operator() and call_at on the way out, so the type a caller
// sees keeps the guarantee it had. What that costs is argument capture for
// parameter types whose copy can throw -- see ArgsAreNothrowStorable, and
// tracks_args, which reports it.
//
// A C-style variadic function is still rejected, but it no longer needs a trait
// of its own to catch it. It used to: deduction against ret(*)(args...) SUCCEEDS
// for int(*)(int, ...) with args = {int}, the ellipsis simply dropped, and the
// wrapper would silently have become a one-parameter function. Matching on the
// function type instead of the pointer value (see detail::FunctionImpl) removes
// that conversion entirely -- int(int, ...) is not int(int) and matches no
// specialization -- so the shape lands on the primary and is reported there.
//
// The one combination still rejected is a noexcept C-style variadic function,
// and it is rejected for the ellipsis, not for the noexcept.

#define TEMPO_C_VARIADIC_MESSAGE                                                   \
    "  A function declared with a trailing '...' (printf-like) cannot be wrapped\n"\
    "  faithfully: tempo would build a wrapper from the NAMED parameters only and\n"\
    "  quietly drop the '...', so calls passing variadic arguments would stop\n"    \
    "  compiling and the signature would no longer be the one you wrote.\n"         \
    "  Fix: wrap the calls you want measured in a lambda --\n"                      \
    "      auto m = tempo::measure([]{ return my_printf_like(3, 10, 20, 30); });"

// The signature stand-in, for the same reason: Functor reads ReturnType,
// ArgsType, is_const and total_arg_size off it.
struct UnsupportedSignature {
    using ReturnType = UnsupportedReturn;
    using ArgsType   = std::tuple<UnsupportedArg>;
    static constexpr bool is_const = true;
    static constexpr bool is_noexcept = false;
    static constexpr std::size_t total_arg_size = 0;
};

// Decomposes the signature of a member function pointer. This is how we read the
// signature of lambdas and functors, via &F::operator().
//
// The primary template is the failure case: a member function whose shape none
// of the specializations below match. It reports that instead of being left
// incomplete, which is what produced "invalid use of incomplete type".
template <typename MemberPointer>
struct MemberSignature : UnsupportedSignature {
    static_assert(always_false<MemberPointer>,
        "tempo: this callable object's operator() has a shape tempo cannot read.\n"
        "  tempo supports a plain operator(), optionally const and/or noexcept.\n"
        "  It does NOT support a ref-qualified operator() (declared with a trailing\n"
        "  '&' or '&&'), a volatile operator(), or a C-style variadic one ('...').\n"
        "  Fix: declare the functor's operator() without the ref-qualifier, or wrap\n"
        "  the object in a lambda with concrete parameter types and pass that to\n"
        "  tempo::measure(...).");
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...)> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = false;
    static constexpr bool is_noexcept = false;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...) const> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = true;
    static constexpr bool is_noexcept = false;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...) noexcept> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = false;
    static constexpr bool is_noexcept = true;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...) const noexcept> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = true;
    static constexpr bool is_noexcept = true;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

// Scrapes the type name out of __PRETTY_FUNCTION__, so the summary table shows a
// readable name like "Callable<fibonacci>". typeid(...).name() would be mangled
// and unreadable.
template <typename T>
constexpr std::string_view type_name() {
#if defined(__clang__)
    constexpr std::string_view text = __PRETTY_FUNCTION__;
    constexpr std::string_view prefix = "[T = ";
    constexpr std::string_view suffix = "]";
#elif defined(__GNUC__)
    constexpr std::string_view text = __PRETTY_FUNCTION__;
    constexpr std::string_view prefix = "with T = ";
    constexpr std::string_view suffix = ";";
#else
    constexpr std::string_view text = __FUNCSIG__;
    constexpr std::string_view prefix = "type_name<";
    constexpr std::string_view suffix = ">(";
#endif
    const auto begin = text.find(prefix) + prefix.size();
    auto end = text.find(suffix, begin);
    if (end == std::string_view::npos) { end = text.rfind(']'); }
    return text.substr(begin, end - begin);
}

// The registry behind aggregated reporting. On its first call, every Metrics
// instantiation drops a function here that fetches its own row; tempo::report()
// collects them all and prints one sorted table.
struct ReportRow {
    std::string name;
    CallCount calls = 0;
    double total_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    bool has_samples = false;
    unsigned int max_depth = 0;
    CallCount timed_calls = 0;

    // Divided by outermost calls, not by every recursive level. See
    // Metrics::timed_calls.
    double average_ms() const { return timed_calls ? total_ms / timed_calls : 0.0; }
};

using RowFetcher = ReportRow (*)();
using Resetter = void (*)();

struct Registry {
    std::mutex mutex;
    std::vector<RowFetcher> fetchers;
    std::vector<Resetter> resetters;
};

// A function-local static: no static initialization order problem, it is
// constructed on first use.
inline Registry& registry() {
    static Registry instance;
    return instance;
}

inline void add_to_registry(RowFetcher fetcher, Resetter resetter) {
    Registry& reg = registry();
    const std::lock_guard<std::mutex> guard{reg.mutex};
    reg.fetchers.push_back(fetcher);
    reg.resetters.push_back(resetter);
}

} // namespace detail

// A summary of every registered metric: one table, sorted by total time.
inline void report(std::ostream& out = std::cout) {
    std::vector<detail::ReportRow> rows;
    {
        detail::Registry& reg = detail::registry();
        const std::lock_guard<std::mutex> guard{reg.mutex};
        rows.reserve(reg.fetchers.size());
        for (const detail::RowFetcher fetch : reg.fetchers) { rows.push_back(fetch()); }
    }

    std::erase_if(rows, [](const detail::ReportRow& row) { return row.calls == 0; });
    std::ranges::sort(rows, [](const auto& left, const auto& right) {
        return left.total_ms > right.total_ms;
    });

    out << "\n=== tempo report ===================================================\n";
    if (rows.empty()) {
        out << "(no calls recorded)\n";
        return;
    }

    std::size_t width = 8;
    for (const auto& row : rows) { width = std::max(width, row.name.size()); }
    width = std::min<std::size_t>(width, 60);

    // The depth column appears only when something actually recursed through a
    // wrapper. A program without recursion prints exactly the table it always
    // printed.
    const bool show_depth = std::ranges::any_of(
        rows, [](const detail::ReportRow& row) { return row.max_depth > 1; });
    const std::size_t rule = width + 56 + (show_depth ? 8 : 0);

    out << std::left << std::setw(static_cast<int>(width)) << "callable"
        << std::right
        << std::setw(8)  << "calls"
        << std::setw(12) << "total ms"
        << std::setw(12) << "avg ms"
        << std::setw(12) << "min ms"
        << std::setw(12) << "max ms";
    if (show_depth) { out << std::setw(8) << "depth"; }
    out << "\n";
    out << std::string(rule, '-') << "\n";

    for (const auto& row : rows) {
        std::string name = row.name;
        if (name.size() > width) { name = name.substr(0, width - 3) + "..."; }
        out << std::left << std::setw(static_cast<int>(width)) << name
            << std::right << std::fixed << std::setprecision(4)
            << std::setw(8)  << row.calls
            << std::setw(12) << row.total_ms
            << std::setw(12) << row.average_ms()
            << std::setw(12) << row.min_ms
            << std::setw(12) << row.max_ms;
        if (show_depth) { out << std::setw(8) << row.max_depth; }
        out << "\n";
    }
    out << std::string(rule, '=') << "\n";
}

// Resets every registered metric.
inline void reset_all() {
    detail::Registry& reg = detail::registry();
    const std::lock_guard<std::mutex> guard{reg.mutex};
    for (const detail::Resetter reset : reg.resetters) { reset(); }
}

// For those who want the summary printed automatically when the program ends.
inline void report_at_exit(std::ostream& out = std::cout) {
    struct AtExit {
        std::ostream* stream;
        ~AtExit() { report(*stream); }
    };
    static AtExit guard{&out};   // its destructor runs at program exit
    (void)guard;
}

// A callable object: lambda, functor, std::function. It needs a single,
// non-template operator() -- generic lambdas ([](auto x){...}) and functors with
// an overloaded operator() are rejected here, because they have no signature
// until they are called. The rejection is not silent: you get a constraint-not-
// satisfied error.
template <typename F>
concept CallableObject =
    std::is_class_v<F> &&
    requires { &F::operator(); };

namespace detail {

// Reads a callable object's signature off its operator(). The unconstrained
// primary is the failure case -- F has no single operator() to take the address
// of -- and yields the stand-in, so Functor can report the problem itself rather
// than dying on "&F::operator()" halfway through its own body.
template <typename F>
struct FunctorSignature {
    using Type = UnsupportedSignature;
};

template <typename F>
requires CallableObject<F>
struct FunctorSignature<F> {
    using Type = MemberSignature<decltype(&F::operator())>;
};

// What Metrics and Profiler need from whatever they are told to wrap. Used to
// catch tempo::Metrics<MyLambda>, which should have been tempo::measure(lambda):
// without this the class would die reading ::ReturnType off a type that has
// none, before its own static_assert could be reached.
template <typename W>
concept TempoWrapper = requires {
    typename W::ReturnType;
    typename W::ArgsType;
    { W::is_member } -> std::convertible_to<bool>;
};

template <typename W>
struct WrapperOrStandIn {
    using Type = UnsupportedCallable;
};

template <typename W>
requires TempoWrapper<W>
struct WrapperOrStandIn<W> {
    using Type = W;
};

#define TEMPO_NOT_A_WRAPPER_MESSAGE                                                \
    "  tempo::Metrics and tempo::Profiler wrap a tempo wrapper type, not a raw\n"  \
    "  function or lambda type. Use one of these instead:\n"                       \
    "      TEMPO_CALLABLE_METRICS(my_function)   // or TEMPO_INSTRUMENT(...)\n"     \
    "      auto m = tempo::measure(my_lambda);   // for lambdas and functors\n"     \
    "  Spelled out, the wrapped type is tempo::Callable<&my_function> for a\n"      \
    "  function and tempo::Functor<decltype(my_lambda)> for a lambda."

// Shared by every "you called it wrong" assert, so the explanation cannot drift
// between the call operator, call_at and Profiler.
#define TEMPO_BAD_CALL_ARGUMENTS_MESSAGE                                           \
    "  Check the number, order and types of the arguments against the callable's\n"\
    "  own signature.\n"                                                           \
    "  For a MEMBER function the FIRST argument is the object itself, and the\n"    \
    "  method's own arguments follow it:\n"                                        \
    "      tempo::CallableMetrics<&Service::handle> m;\n"                          \
    "      m(service, 42);        // object first, then the method's arguments\n"   \
    "  The object may be passed as Service&, Service*, a std::reference_wrapper\n"  \
    "  or a smart pointer -- but it may not be left out."

// The wording is shared by Functor and by the three factories, so that a user
// who reaches this from tempo::measure and one who reaches it from tempo::wrap
// read the same explanation.
#define TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE                                        \
    "  A callable object must be a class with exactly ONE plain, non-template\n"   \
    "  operator(). These are rejected, because they have no signature until they\n"\
    "  are actually called:\n"                                                     \
    "    - generic lambdas:      [](auto x){ ... }\n"                              \
    "    - templated operator(): template <typename T> void operator()(T)\n"        \
    "    - overloaded operator(): two or more operator() in the same class\n"      \
    "  Fix: give the lambda concrete parameter types -- [](int x){ ... } instead\n" \
    "  of [](auto x){ ... }. For a plain function use TEMPO_INSTRUMENT or\n"        \
    "  TEMPO_CALLABLE_METRICS instead of a factory."

} // namespace detail

namespace detail {

// Why Function and Method below are keyed on the callable's TYPE and not on the
// pointer value, which is the obvious thing and does not work.
//
// A pointer to a noexcept function converts implicitly to a pointer to a plain
// one. Deducing ret(*)(args...) from a noexcept pointer therefore requires a
// conversion, and the compilers do not agree about it: GCC refuses and falls to
// the primary template, while Clang performs it and drops the qualifier where no
// trait inside the specialization can read it back. Adding a second partial
// specialization for the noexcept shape does not settle it either -- both then
// match on Clang, and it reports them as ambiguous. Constraints do not break the
// tie, because from inside the plain specialization the noexcept is already gone
// and there is nothing left to constrain on.
//
// Matching on the function TYPE has no conversion in it. ret(args...) and
// ret(args...) noexcept are distinct types, each matches exactly one
// specialization, and both compilers agree -- which also means the C-style
// variadic shape lands on the primary here instead of quietly losing its
// ellipsis to deduction, as it used to.
//
// The body is shared rather than written out per shape, since the exception
// specification is the only thing that differs.
template <auto func_ptr, bool Noexcept, typename ret, typename... args>
struct FunctionBody {
    using  ReturnType = ret;
    using  ArgsType   = std::tuple<args...>;
    using  ClassType  = void;
    static constexpr bool is_member = false;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = false;
    static constexpr bool is_noexcept = Noexcept;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
    inline static std::atomic<CallCount> call_count{0};

    // The parameters are forwarding references: an argument reaches func_ptr with
    // its own value category, without a copy in between. The std::invocable
    // constraint keeps the error at the call site rather than inside std::invoke.
    //
    // noexcept follows the function, so the wrapper's type carries the same
    // guarantee it had. Nothing else in here can throw: the counter is atomic.
    template <typename... CallArgs>
        requires std::invocable<decltype(func_ptr), CallArgs...>
    ReturnType operator()(CallArgs&&... call_args) const noexcept(Noexcept) {
        call_count++;
        return std::invoke(func_ptr, std::forward<CallArgs>(call_args)...);
    }
};

// The primary is the failure case: a function type none of the specializations
// match, which in practice means a C-style variadic one. Reporting it here is
// what keeps TEMPO_INSTRUMENT(impl::f, f) on an unsupported function to a single
// message.
template <typename Signature, auto func_ptr>
struct FunctionImpl : UnsupportedCallable {
    static_assert(always_false_value<func_ptr>,
        "tempo: this function's type is not supported.\n"
        "  tempo matches function pointers of the form  ret(*)(args...), with or\n"
        "  without 'noexcept'.\n"
        TEMPO_C_VARIADIC_MESSAGE);
};

template <typename ret, typename... args, auto func_ptr>
struct FunctionImpl<ret(args...), func_ptr>
    : FunctionBody<func_ptr, false, ret, args...> {};

template <typename ret, typename... args, auto func_ptr>
struct FunctionImpl<ret(args...) noexcept, func_ptr>
    : FunctionBody<func_ptr, true, ret, args...> {};

} // namespace detail

template<auto Func>
requires FunctionPointer<Func>
struct Function : detail::FunctionImpl<std::remove_pointer_t<decltype(Func)>, Func> {};

namespace detail {

// The member-function counterpart of FunctionBody and FunctionImpl, keyed on the
// pointer type for the same reason -- see the comment above FunctionBody. There
// are four shapes here rather than two, because const and noexcept are
// independent.
template <auto method, bool Noexcept, bool Const, typename ClassName, typename ret, typename... args>
struct MethodBody {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    using ClassType = ClassName;
    static constexpr bool is_member = true;
    static constexpr bool is_const_member = Const;
    static constexpr bool is_functor = false;
    static constexpr bool is_noexcept = Noexcept;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) +  ... +  0);
    inline static std::atomic<CallCount> call_count{0};

    // The instance is forwarded too: thanks to std::invoke, ClassName&,
    // ClassName*, std::reference_wrapper and smart pointers all work.
    template <typename Self, typename... CallArgs>
        requires std::invocable<decltype(method), Self, CallArgs...>
    ReturnType operator()(Self&& self, CallArgs&&... call_args) const noexcept(Noexcept) {
        call_count++;
        return std::invoke(method, std::forward<Self>(self), std::forward<CallArgs>(call_args)...);
    }
};

// The primary is the failure case: a ref-qualified, volatile or C-style variadic
// member function, none of which match a specialization below.
template <typename Signature, auto method>
struct MethodImpl : UnsupportedCallable {
    static_assert(always_false_value<method>,
        "tempo: this member function's type is not supported.\n"
        "  tempo matches member function pointers of the form  ret(Class::*)(args...),\n"
        "  their const version, and either of those declared 'noexcept'.\n"
        "  A ref-qualified member function (declared with a trailing '&' or '&&'), a\n"
        "  volatile one, or a C-style variadic one ('...') does not match.\n"
        "  Fix: wrap the call in a lambda that captures the object and measure that --\n"
        "      auto m = tempo::measure([&obj](int a){ return obj.my_method(a); });");
};

template <typename ClassName, typename ret, typename... args, auto method>
struct MethodImpl<ret (ClassName::*)(args...), method>
    : MethodBody<method, false, false, ClassName, ret, args...> {};

template <typename ClassName, typename ret, typename... args, auto method>
struct MethodImpl<ret (ClassName::*)(args...) const, method>
    : MethodBody<method, false, true, ClassName, ret, args...> {};

template <typename ClassName, typename ret, typename... args, auto method>
struct MethodImpl<ret (ClassName::*)(args...) noexcept, method>
    : MethodBody<method, true, false, ClassName, ret, args...> {};

template <typename ClassName, typename ret, typename... args, auto method>
struct MethodImpl<ret (ClassName::*)(args...) const noexcept, method>
    : MethodBody<method, true, true, ClassName, ret, args...> {};

} // namespace detail

template<auto MethodValue>
requires MethodPointer<MethodValue>
struct Method : detail::MethodImpl<decltype(MethodValue), MethodValue> {};

// The unconstrained primary is the failure case -- CallableValue is neither a
// function pointer nor a member function pointer. It resolves to the stand-in so
// that Callable below can report the problem itself, in one message, instead of
// the constraint failing here and the user being told only "constraints not
// satisfied".
template<auto CallableValue>
struct CallableImplementation {
    using Type = detail::UnsupportedCallable;
};

template<auto CallableValue>
requires FunctionPointer<CallableValue>
struct CallableImplementation<CallableValue> {
    using Type = Function<CallableValue>;
};

template<auto CallableValue>
requires MethodPointer<CallableValue>
struct CallableImplementation<CallableValue> {
    using Type = Method<CallableValue>;
};

template<auto CallableValue>
struct Callable : CallableImplementation<CallableValue>::Type {
    // The single most common mistake: pointing the macros at a lambda or a
    // functor. Those are objects, and an object with state can never be a
    // template argument -- which is why the factories exist.
    static_assert(SupportedCallable<CallableValue>,
        "tempo: the template argument is not a function or member function pointer.\n"
        "  TEMPO_INSTRUMENT, TEMPO_CALLABLE_METRICS and TEMPO_CALLABLE_PROFILER take\n"
        "  the ADDRESS OF A FUNCTION:\n"
        "      TEMPO_INSTRUMENT(impl::my_function, my_function);\n"
        "  If you passed a lambda, a functor or a std::function, use a factory\n"
        "  instead -- those are objects, not function pointers, and an object\n"
        "  cannot be a template argument:\n"
        "      auto m = tempo::measure(my_lambda);\n"
        "      TEMPO_METRICS_CALL(m, arg1, arg2);");

    // There used to be two more asserts here, rejecting noexcept callables. They
    // are gone: Function and Method now have exact specializations for the
    // noexcept shapes, so both compilers match one and neither has to drop the
    // qualifier to get there. What made this the right place for the check --
    // that the template argument's own type is still intact here, while inside a
    // specialization Clang may already have dropped the noexcept -- is exactly
    // what an exact specialization fixes at the source.

    using CallableType = typename CallableImplementation<CallableValue>::Type;
    using CallableType::operator();
};

// Function and Method hold no state, so their template argument can be a pointer
// (an NTTP). Lambdas and functors, however, are OBJECTS: a capturing lambda can
// never be an NTTP. That is why Functor is templated on the type and carries the
// callable object itself inside.
// Deliberately unconstrained, so that a wrong F produces the message below
// rather than a bare "constraints not satisfied". The signature lookup goes
// through detail::FunctorSignature, which tolerates an F that has no readable
// operator() -- taking &F::operator() directly here would be a hard error before
// the assert could ever be reached.
template <typename F>
struct Functor {

    static_assert(CallableObject<F>,
        "tempo: this is not a callable object tempo can read.\n"
        TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE);

    using SignatureType = typename detail::FunctorSignature<F>::Type;
    using ReturnType = typename SignatureType::ReturnType;
    using ArgsType   = typename SignatureType::ArgsType;
    using ClassType  = F;

    // is_member is false because the caller does not pass the instance
    // separately; the object lives inside the wrapper.
    static constexpr bool is_member = false;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = true;
    static constexpr bool is_const_callable = SignatureType::is_const;
    // Read off operator(), so a lambda declared [](int) noexcept {...} gets a
    // noexcept wrapper exactly as a noexcept function does.
    static constexpr bool is_noexcept = SignatureType::is_noexcept;
    static constexpr auto arg_count = std::tuple_size_v<ArgsType>;
    static constexpr auto total_arg_size = SignatureType::total_arg_size;

    // The counter is tied to the type. Since every lambda EXPRESSION produces its
    // own unique closure type, this means a separate counter per lambda. Two
    // objects of the same type (two std::function<int(int)>, say) SHARE a counter.
    inline static std::atomic<CallCount> call_count{0};

    // mutable: the operator() of a mutable lambda is not const, but the Profiler
    // and Metrics chain calls through a const path.
    mutable F target;

    template <typename... CallArgs>
        requires std::invocable<F&, CallArgs...>
    ReturnType operator()(CallArgs&&... call_args) const noexcept(is_noexcept) {
        call_count++;
        // The `if constexpr` serves only the failure case above, where ReturnType
        // is the stand-in while the object itself returns void. On the real path
        // ReturnType is read off this very operator(), so the two always agree
        // and the else branch is the one taken -- it still returns the call
        // expression directly, so the return value is never copied or moved.
        if constexpr (std::is_void_v<std::invoke_result_t<F&, CallArgs...>> &&
                      !std::is_void_v<ReturnType>) {
            std::invoke(target, std::forward<CallArgs>(call_args)...);
            return ReturnType{};
        }
        else {
            return std::invoke(target, std::forward<CallArgs>(call_args)...);
        }
    }
};


namespace detail {

// Is W one of tempo's own wrapper templates?
//
// Metrics and Profiler accept this as well as TempoWrapper, because a wrapper
// whose own static_assert has already fired may stop looking like one. Clang
// marks a class containing a failed assert as invalid, so every later member
// lookup on it fails too -- and Metrics would then print "this is not a tempo
// wrapper type" underneath the message that had already explained the real
// problem. GCC keeps the members visible and prints only the first. Checking the
// template itself makes the two agree: if it is one of ours, the diagnosis has
// been delivered elsewhere and there is nothing to add here.
template <typename W>
inline constexpr bool is_tempo_wrapper_template = false;

template <auto V>
inline constexpr bool is_tempo_wrapper_template<Callable<V>> = true;
template <auto V>
    requires FunctionPointer<V>
inline constexpr bool is_tempo_wrapper_template<Function<V>> = true;
template <auto V>
    requires MethodPointer<V>
inline constexpr bool is_tempo_wrapper_template<Method<V>> = true;
template <typename F>
inline constexpr bool is_tempo_wrapper_template<Functor<F>> = true;
template <>
inline constexpr bool is_tempo_wrapper_template<UnsupportedCallable> = true;

} // namespace detail

// TMP is cleaner since C++20.
//
// Templated on the wrapper type (Callable<&f> or Functor<Lambda>), not on an
// NTTP: a lambda cannot be a template argument, but its type can.
template <typename WrapperType>
struct Profiler{

    static_assert(detail::TempoWrapper<WrapperType> ||
                  detail::is_tempo_wrapper_template<WrapperType>,
        "tempo::Profiler: this is not a tempo wrapper type.\n"
        TEMPO_NOT_A_WRAPPER_MESSAGE);

    // Falls back to the stand-in when the assert above fires, so that one
    // message is not followed by "no type named ReturnType".
    using CallableType = typename detail::WrapperOrStandIn<WrapperType>::Type;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType = typename CallableType::ArgsType;

    using SourceLocation = std::source_location;

    // The lock guarding shared state. It is NEVER held while the user's function
    // runs -- it is taken only for short critical sections, so there is neither a
    // deadlock risk nor any serialization of the code being measured.
    inline static std::mutex state_mutex;
    inline static SourceLocation last_call_location{};

    static SourceLocation get_last_call_location() {
        const std::lock_guard<std::mutex> guard{state_mutex};
        return last_call_location;
    }

    CallableType callable;


    // Whether the wrapped callable promised not to throw, and therefore whether
    // this wrapper does too.
    static constexpr bool is_noexcept = CallableType::is_noexcept;

    // noexcept follows the callable, as in Metrics. Unlike Metrics this path
    // takes the lock BEFORE the call, to keep two threads' report lines from
    // interleaving -- so a mutex that fails to lock terminates here rather than
    // unwinding. That is the same lock, in the same state, that the destructor
    // below has always taken with no way to unwind either; std::mutex::lock only
    // throws on errors a correct program cannot produce.
    ReturnType call_at(SourceLocation location, auto&&... args) const
        noexcept(is_noexcept)
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        {
            const std::lock_guard<std::mutex> guard{state_mutex};
            last_call_location = location;
#if TEMPO_PRINT_ENABLED
            std::cout << "[CallableProfiler] Starting execution...\n";
            std::cout << "[CallableProfiler] Call location: " << location.file_name() << ":" << location.line() << "\n";
            std::cout << "[CallableProfiler] Caller function: " << location.function_name() << "\n";
            std::cout << "[CallableProfiler] Total size of args:" << CallableType::total_arg_size << " bytes\n";
#endif
        }

        // The post-call work happens in a destructor, which lets us return the
        // call expression directly. Because there is no named local variable, the
        // return value is never copied or moved (guaranteed copy elision).
        [[maybe_unused]] const ReportOnExit report{};
        return callable(std::forward<decltype(args)>(args)...);
    }

    ReturnType operator()(auto&&... args) const
        noexcept(is_noexcept)
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return call_at(SourceLocation::current(), std::forward<decltype(args)>(args)...);
    }

    // As in Metrics: selected only when the call above is not viable, purely to
    // say what was wrong with the arguments.
    ReturnType operator()(auto&&... args) const
        requires (!std::invocable<const CallableType&, decltype(args)...>)
    {
        static_assert(detail::always_false<decltype(args)...>,
            "tempo::Profiler: this callable cannot be invoked with the arguments you passed.\n"
            TEMPO_BAD_CALL_ARGUMENTS_MESSAGE);
    }

private:
    struct ReportOnExit {
        int exceptions_on_entry = std::uncaught_exceptions();

        ~ReportOnExit() {
            if (std::uncaught_exceptions() != exceptions_on_entry) {
                return; // the call threw, do not report the counter
            }
#if TEMPO_PRINT_ENABLED
            const std::lock_guard<std::mutex> guard{state_mutex};
            std::cout << "[CallableProfiler] Call count: " << CallableType::call_count << "\n";
#endif
        }
    };
};

// The old name is kept so TEMPO_CALLABLE_PROFILER and existing code still work.
// Deliberately unconstrained: Callable already carries a static_assert that says
// what a wrong template argument should have been, and a constraint here would
// pre-empt it with "constraints not satisfied" and nothing else.
template <auto CallableValue>
using CallableProfiler = Profiler<Callable<CallableValue>>;
//-------------------------------------------------------------------
namespace detail {

// The two shapes of operator() that Metrics can expose. Which one you get
// depends on whether the wrapped callable is a member function, and the choice
// is not cosmetic -- it decides whether a bare call can capture its own call
// site.
//
// Only ONE of them is ever inherited. Having both would be worse than useless:
// for fib(26) with a parameter of type unsigned, the variadic template binds
// int&& exactly while the fixed signature needs an int -> unsigned conversion,
// so the template would win overload resolution and silently take the location
// away again.

// Used for member functions. The caller supplies the instance, and we keep
// accepting every form std::invoke understands: ClassName&, ClassName*,
// std::reference_wrapper, smart pointers. A fixed signature cannot express that,
// and members cannot use the TEMPO_INSTRUMENT seam anyway (there is no free name
// for a wrapper variable to shadow), so nothing is lost by staying variadic.
template <typename Derived, typename CallableType>
struct VariadicCall {
    typename CallableType::ReturnType operator()(auto&&... args) const
        noexcept(CallableType::is_noexcept)
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return static_cast<const Derived&>(*this).call_at(
            std::source_location::current(), std::forward<decltype(args)>(args)...);
    }

    // Selected only when the overload above is not viable, and does nothing but
    // explain why. Without it the compiler reports "no match for call to
    // (tempo::Metrics<...>) (Foo&, int)" and leaves the reader to work out which
    // of the arguments was wrong.
    typename CallableType::ReturnType operator()(auto&&... args) const
        requires (!std::invocable<const CallableType&, decltype(args)...>)
    {
        static_assert(always_false<decltype(args)...>,
            "tempo: this callable cannot be invoked with the arguments you passed.\n"
            TEMPO_BAD_CALL_ARGUMENTS_MESSAGE);
    }
};

// Used for free functions and callable objects: the callable's exact parameter
// list, plus a trailing source_location that defaults to current().
//
// The reason this works is that Args... comes from the enclosing class template
// and is already fixed -- it is NOT deduced from the call. That makes this an
// ordinary function with N+1 parameters whose last one has a default argument,
// and a default argument is evaluated AT THE CALL SITE. So a plain fib(26) now
// records the caller's file and line, with no macro. You cannot do this with a
// deduced pack: a default argument may not follow one.
//
// The price is that parameters arrive by their declared types rather than by
// forwarding reference. For reference parameters that costs exactly nothing; for
// a by-value parameter handed an lvalue it costs one extra move.
template <typename Derived, typename CallableType, typename ArgsTuple>
struct FixedSignatureCall;

template <typename Derived, typename CallableType, typename... Args>
struct FixedSignatureCall<Derived, CallableType, std::tuple<Args...>> {
    // noexcept follows the callable, so an instrumented name keeps the exception
    // specification the function it stands for had. This is what makes
    // TEMPO_INSTRUMENT usable on a noexcept function at all: the seam works by
    // the wrapper taking the function's name, so the wrapper's type is the type
    // every caller now sees.
    typename CallableType::ReturnType operator()(
        Args... args,
        std::source_location location = std::source_location::current()) const
        noexcept(CallableType::is_noexcept)
    {
        return static_cast<const Derived&>(*this).call_at(
            location, static_cast<Args&&>(args)...);
    }
};

template <typename Derived, typename CallableType>
using CallOperator = std::conditional_t<
    CallableType::is_member,
    VariadicCall<Derived, CallableType>,
    FixedSignatureCall<Derived, CallableType, typename CallableType::ArgsType>>;

} // namespace detail

// The measuring side does NOT delegate to Profiler. Profiler writes five lines on
// every call; if Metrics went through it, that I/O would land inside the
// measurement window and a single call's "duration" would start with a few
// microseconds of cout cost. The only thing timed here is the call itself;
// reporting happens after the clock has stopped.
template <typename WrapperType>
struct Metrics : detail::CallOperator<Metrics<WrapperType>,
                                      typename detail::WrapperOrStandIn<WrapperType>::Type> {

    static_assert(detail::TempoWrapper<WrapperType> ||
                  detail::is_tempo_wrapper_template<WrapperType>,
        "tempo::Metrics: this is not a tempo wrapper type.\n"
        TEMPO_NOT_A_WRAPPER_MESSAGE);

    // The stand-in stands in when the assert above fires. It has to be chosen
    // here AND in the base clause above, because the base is formed before the
    // body is looked at -- reading ::is_member off a non-wrapper there would
    // fail before the assert could speak.
    using CallableType = typename detail::WrapperOrStandIn<WrapperType>::Type;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType   = typename CallableType::ArgsType;
    using SourceLocation = std::source_location;

    // Metrics declares no operator() of its own -- it inherits exactly one, so
    // there is never an overload to resolve between. See detail::CallOperator.
    using CallOperatorBase = detail::CallOperator<Metrics<WrapperType>, CallableType>;
    using CallOperatorBase::operator();
    // steady_clock, NOT high_resolution_clock. On libstdc++ high_resolution_clock
    // is an alias for system_clock (is_steady == false): if the wall clock is
    // stepped backwards by NTP, the difference between two Clock::now() calls
    // comes out negative or nonsensical. The only correct clock for measuring
    // durations is a monotonic one.
    using Clock = std::chrono::steady_clock;
    static_assert(Clock::is_steady, "tempo requires a monotonic clock to measure durations");
    using Duration = std::chrono::duration<double, std::milli>;

    // Whether the wrapped callable promised not to throw, and therefore whether
    // this wrapper does too.
    static constexpr bool is_noexcept = CallableType::is_noexcept;

    // Argument capture is off when any parameter cannot be stored -- and, for a
    // noexcept callable, also when storing it could throw. The wrapper's
    // noexcept is a promise made to its callers, and the copy tempo takes to
    // record a call is the one allocation it would be broken by; a std::string
    // parameter on a noexcept function is the ordinary case. Timing, counts and
    // the report are unaffected either way. See ArgsAreNothrowStorable.
    static constexpr bool tracks_args =
        detail::ArgsAreStorable<ArgsType>::value &&
        (!is_noexcept || detail::ArgsAreNothrowStorable<ArgsType>::value);

    // The signature with its references stripped: storable and assignable.
    using StoredArgsType = std::conditional_t<
        tracks_args,
        typename detail::DecayedTuple<ArgsType>::Type,
        std::tuple<>>;

    // call_count is atomic and lives on the wrapper, so it can be read directly.
    inline static auto& call_count = CallableType::call_count;

private:
    // The collected statistics are private: all of them are guarded by
    // stats_mutex and are read from outside only through snapshot(), as one
    // consistent whole. Putting an atomic counter next to unsynchronized totals
    // would advertise a guarantee that does not exist.
    inline static std::mutex stats_mutex;
    inline static bool has_samples = false;

    // Outermost calls only -- the ones total/min/max actually describe. Without
    // recursion this equals call_count. With it, call_count counts every level
    // while only the outermost is timed, so dividing total by call_count would
    // report an average per recursive step against a total that never included
    // them. This is the denominator the average needs.
    inline static CallCount timed_calls = 0;

    inline static Duration total_duration{0};
    inline static Duration max_duration{0};
    inline static Duration min_duration{0};
    inline static StoredArgsType min_args{};
    inline static StoredArgsType max_args{};
    inline static SourceLocation last_call_location{};
    inline static unsigned int max_depth = 0;

    // Recursion depth, per thread: two threads recursing independently each have
    // their own notion of "outermost". Untouched by the mutex on purpose -- it is
    // read and written on every single call, including the deep interior of a
    // recursion, and taking a lock there would cost more than everything we are
    // trying to measure.
    inline static thread_local unsigned int depth = 0;

    // The deepest this thread has gone within the current outermost call. Merged
    // into the shared max_depth once, when that outermost call returns, so the
    // interior of a recursion never touches the lock.
    inline static thread_local unsigned int peak_depth = 0;

    // Returns true when this call is the outermost one. Called before the clock
    // starts, so the bookkeeping is never part of a measurement.
    static bool enter_depth() {
        const unsigned int current = ++depth;
        // Resetting on the way in rather than on the way out keeps this correct
        // when a call throws: the unwinding path never reaches the merge, so a
        // stale peak would otherwise be credited to the next call.
        if (current == 1) { peak_depth = 1; }
        else if (current > peak_depth) { peak_depth = current; }
        return current == 1;
    }

public:
    // A consistent view taken in one go. Reading the total and the min separately
    // would not just be a data race, it would be INCONSISTENT: one could reflect
    // the state before an update and the other the state after it.
    struct Snapshot {
        CallCount calls = 0;
        Duration total_duration{0};
        Duration min_duration{0};
        Duration max_duration{0};
        StoredArgsType min_args{};
        StoredArgsType max_args{};
        SourceLocation last_call_location{};
        bool has_samples = false;

        // Deepest recursion reached. 1 for an ordinary function, 0 before the
        // first call. Only ever above 1 when recursion goes through the wrapper,
        // i.e. TEMPO_SELF with TEMPO_COUNT_RECURSION=1.
        unsigned int max_depth = 0;

        // Outermost calls. Equals calls unless recursion is being counted.
        CallCount timed_calls = 0;

        // Time per outermost call, which is what total_duration measures.
        double average_ms() const {
            return timed_calls ? total_duration.count() / timed_calls : 0.0;
        }
    };

    static Snapshot snapshot() {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return Snapshot{call_count.load(std::memory_order_relaxed),
                        total_duration, min_duration, max_duration,
                        min_args,       max_args,     last_call_location,
                        has_samples,    max_depth,    timed_calls};
    }

    static SourceLocation get_last_call_location() {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return last_call_location;
    }

    // How deep this thread is inside the callable right now: 0 when not in it,
    // 1 in an ordinary call, more only while recursing through the wrapper. No
    // lock, because depth is thread_local and this thread is the only writer.
    static unsigned int current_depth() { return depth; }

    // We hold the wrapper directly: in the Functor case the callable object
    // itself lives here, and must not be reconstructed on every call.
    CallableType callable;

    // Note: we deliberately do NOT forward here. The arguments will be forwarded
    // to the real call and may be left moved-from by it, so we take the snapshot
    // BEFORE the call and by copy, to make sure the values we store are correct.
    static StoredArgsType make_args_snapshot(const auto&... args) {
        if constexpr (!tracks_args) {
            return StoredArgsType{};
        }
        else if constexpr (CallableType::is_member) {
            return make_args_snapshot_without_instance(args...);
        }
        else {
            return StoredArgsType{args...};
        }
    }

    static void reset() {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        call_count.store(0, std::memory_order_relaxed);
        has_samples = false;
        timed_calls = 0;
        total_duration = Duration{0};
        max_duration = Duration{0};
        min_duration = Duration{0};
        min_args = StoredArgsType{};
        max_args = StoredArgsType{};
        last_call_location = SourceLocation{};
        max_depth = 0;
        // depth itself is deliberately NOT reset: it belongs to whichever call is
        // on the stack right now, and zeroing it mid-recursion would make the
        // next inner call look outermost and restart the clock.
    }

    // Registers this instantiation with the aggregated report. Thanks to the
    // function-local static it runs exactly once, and C++ already guarantees
    // that to be thread-safe.
    static void ensure_registered() {
        static const bool once = [] {
            detail::add_to_registry(
                [] {
                    const Snapshot state = snapshot();
                    return detail::ReportRow{std::string{detail::type_name<CallableType>()},
                                             state.calls,
                                             state.total_duration.count(),
                                             state.min_duration.count(),
                                             state.max_duration.count(),
                                             state.has_samples,
                                             state.max_depth,
                                             state.timed_calls};
                },
                [] { reset(); });
            return true;
        }();
        (void)once;
    }

    // noexcept follows the callable. Nothing on this path throws when it does:
    // the argument snapshot is restricted to nothrow-storable types (see
    // tracks_args), and the recording that takes the lock happens in
    // RecordOnExit's destructor, which is noexcept already and always was.
    //
    // One residual path is worth naming rather than hiding: ensure_registered()
    // allocates, once, on the first call, and an allocation failure there
    // terminates instead of unwinding. That is the same thing any noexcept
    // function does when it runs out of memory, and it is the price of the
    // aggregated report knowing about a metric without being told.
    ReturnType call_at(SourceLocation location, auto&&... args) const
        noexcept(is_noexcept)
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        ensure_registered();

        // The snapshot is taken before the call (by copy), while the originals are
        // forwarded to the real call. Forwarding to both would end up storing
        // moved-from values.
        StoredArgsType snapshot = make_args_snapshot(args...);

        // The clock starts as the guard is constructed and stops on the FIRST line
        // of its destructor; in between there is nothing but the call itself.
        // Reporting happens after the clock has stopped. Since the return value
        // never passes through a named local it is never copied, so even
        // non-movable types get through.
        [[maybe_unused]] const RecordOnExit record{location, snapshot};
        return callable(std::forward<decltype(args)>(args)...);
    }

    // The TEMPO_METRICS_CALL path's version of the same explanation. A macro call
    // lands here rather than on the call operator, so the message has to exist in
    // both places.
    ReturnType call_at(SourceLocation, auto&&... args) const
        requires (!std::invocable<const CallableType&, decltype(args)...>)
    {
        static_assert(detail::always_false<decltype(args)...>,
            "tempo: TEMPO_METRICS_CALL -- this callable cannot be invoked with these arguments.\n"
            TEMPO_BAD_CALL_ARGUMENTS_MESSAGE);
    }

    // The message both accessors give when this callable's arguments are not
    // being stored. Without it the caller gets an empty tuple and the error
    // surfaces much later, inside <tuple>, as a page about "tuple index out of
    // range" that never mentions tempo.
#define TEMPO_ARGS_NOT_STORED_MESSAGE                                              \
    "  tempo stores a call's arguments only when EVERY parameter type is both\n"   \
    "  copy-constructible and default-constructible. At least one of this\n"        \
    "  callable's parameters is not -- a move-only type such as\n"                  \
    "  std::unique_ptr, a reference member, or a type with no default\n"            \
    "  constructor.\n"                                                              \
    "  If the callable is declared 'noexcept', the rule is stricter: copying,\n"    \
    "  storing and overwriting each parameter must itself be noexcept, because\n"   \
    "  the wrapper carries the same noexcept and the copy tempo takes to record\n"  \
    "  a call is the one place it could throw. A std::string or std::vector\n"      \
    "  parameter on a noexcept function lands here for that reason. Wrapping the\n" \
    "  call in a plain (throwing) lambda and measuring that gets capture back.\n"   \
    "  Timing, call counts and the report are unaffected; only argument capture\n"  \
    "  switches itself off. Query it first with:\n"                                 \
    "      if constexpr (decltype(m)::tracks_args) { ... m.get_maximizers() ... }"

    StoredArgsType get_minimizers() const {
        static_assert(tracks_args,
            "tempo: get_minimizers() is unavailable -- this callable's arguments are not stored.\n"
            TEMPO_ARGS_NOT_STORED_MESSAGE);
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return min_args;
    }
    StoredArgsType get_maximizers() const {
        static_assert(tracks_args,
            "tempo: get_maximizers() is unavailable -- this callable's arguments are not stored.\n"
            TEMPO_ARGS_NOT_STORED_MESSAGE);
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return max_args;
    }

private:
    template <typename Instance, typename... MethodArgs>
    static StoredArgsType make_args_snapshot_without_instance(const Instance&, const MethodArgs&... arg) {
        return StoredArgsType{arg...};
    }

    struct RecordOnExit {
        SourceLocation location;
        StoredArgsType& snapshot;
        int exceptions_on_entry = std::uncaught_exceptions();

        // Depth is taken before the clock starts, so the bookkeeping is outside
        // every measurement. For a non-recursive function this is always true and
        // nothing below behaves differently than it did before depth existed.
        bool outermost = enter_depth();

        // Only the outermost call reads the clock. An inner call is counted --
        // call_count was already incremented by the wrapper -- but never timed,
        // so a deep recursion does not pay for two clock reads per level.
        Clock::time_point start = outermost ? Clock::now() : Clock::time_point{};

        ~RecordOnExit() {
// GCC 15 at -O2 reports -Wdangling-pointer for the assignments of `duration`
// into the static min/max below, but only for instantiations whose
// StoredArgsType is an empty tuple. It is spurious: Duration is
// chrono::duration<double, milli>, a value with no pointer in it, copied into
// static storage. Clang 21 at the same optimization level is silent, and the
// warning disappears at -O0, -O1 and -O3. Suppressed here, as narrowly as the
// diagnostic allows, because a header-only library must not emit warnings into
// its users' builds. If a future GCC stops reporting it, delete the pragmas.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
            // The clock stops before anything else, and before the LOCK in
            // particular: waiting on the lock is never added to the measurement.
            Duration duration{0};
            if (outermost) { duration = Clock::now() - start; }

            // Must happen on every path, including while an exception unwinds,
            // or the depth would stay raised and every later call would look
            // like an inner one and never be timed again.
            --depth;

            if (std::uncaught_exceptions() != exceptions_on_entry) {
                return; // the call threw, do not record a half-finished duration
            }

            // An inner call contributes its count and its depth, nothing else.
            // Timing it would sum intervals that contain one another: with
            // fib(22) that reports about 69 ms of "work" for 4.7 ms of wall time.
            if (!outermost) { return; }

            // One critical section covering both the update and the reporting.
            // The report is inside the lock too, because otherwise the lines from
            // two threads would interleave. The user's function has already
            // returned, so the lock is not holding it.
            const std::lock_guard<std::mutex> guard{stats_mutex};

            // This outermost call is finished, so the peak it reached is final.
            // Merging here keeps the lock out of the recursion's interior;
            // enter_depth() resets the peak for the next outermost call.
            if (peak_depth > max_depth) { max_depth = peak_depth; }

            last_call_location = location;
            ++timed_calls;
            total_duration += duration;

            const bool is_new_max = !has_samples || duration > max_duration;
            const bool is_new_min = !has_samples || duration < min_duration;

            if (is_new_max) { max_duration = duration; }
            if (is_new_min) { min_duration = duration; }
            has_samples = true;

            if constexpr (tracks_args) {
                // We only move the snapshot when it is actually needed; if both
                // fire at once, a single copy is enough.
                if (is_new_max && is_new_min) {
                    max_args = snapshot;
                    min_args = std::move(snapshot);
                }
                else if (is_new_max) { max_args = std::move(snapshot); }
                else if (is_new_min) { min_args = std::move(snapshot); }
            }

#if TEMPO_PRINT_ENABLED
            // Everything below runs after the clock has stopped, so it does not
            // pollute the measurement.
            const auto calls = CallableType::call_count.load(std::memory_order_relaxed);
            std::cout << "[CallableMetrics] Callable ran. Took: " << duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Call location: " << location.file_name() << ":" << location.line() << "\n";
            std::cout << "[CallableMetrics] Caller function: " << location.function_name() << "\n";
            std::cout << "[CallableMetrics] Call count: " << calls << "\n";
            std::cout << "[CallableMetrics] Total size of args: " << CallableType::total_arg_size << " bytes\n";
            std::cout << "[CallableMetrics] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Min time : " << min_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Max time : " << max_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Average time : " << (calls ? total_duration.count() / calls : 0.0) << " ms\n";
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        }
    };

    };

// The old name is kept so TEMPO_CALLABLE_METRICS and existing code still work.
// Unconstrained for the same reason as CallableProfiler: the message lives in
// Callable.
template <auto CallableValue>
using CallableMetrics = Metrics<Callable<CallableValue>>;

//-------------------------------------------------------------------
// Factories for lambdas and functors.
//
// For function pointers we can spell the type name with a macro
// (TEMPO_CALLABLE_METRICS(f)), because &f is a template argument. That is not
// possible for a lambda: we have to pass the object and pick up its type by
// deduction.
template <typename F>
requires CallableObject<std::decay_t<F>>
auto wrap(F&& target) {
    return Functor<std::decay_t<F>>{std::forward<F>(target)};
}

template <typename F>
requires CallableObject<std::decay_t<F>>
auto profile(F&& target) {
    return Profiler<Functor<std::decay_t<F>>>{wrap(std::forward<F>(target))};
}

template <typename F>
requires CallableObject<std::decay_t<F>>
auto measure(F&& target) {
    // The leading {} initializes the inherited call-operator base, which is
    // empty. Metrics is still an aggregate; it just has one more subobject now.
    return Metrics<Functor<std::decay_t<F>>>{{}, wrap(std::forward<F>(target))};
}

// The three overloads below are selected only when the ones above are not
// viable, and exist purely to say why. Without them the compiler reports
// "no matching function for call to measure(...)" followed by a note that a
// constraint was not satisfied, which does not tell a user what to write
// instead.
//
// Each returns a stand-in of the same shape the real factory would have
// returned, so the code that goes on to use the result -- calling it, asking it
// for maximizers -- keeps type-checking and adds no errors of its own.
template <typename F>
requires (!CallableObject<std::decay_t<F>>)
auto wrap(F&&) {
    static_assert(detail::always_false<F>,
        "tempo::wrap: this argument is not a callable object tempo can read.\n"
        TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE);
    return detail::UnsupportedCallable{};
}

template <typename F>
requires (!CallableObject<std::decay_t<F>>)
auto profile(F&&) {
    static_assert(detail::always_false<F>,
        "tempo::profile: this argument is not a callable object tempo can read.\n"
        TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE);
    return Profiler<detail::UnsupportedCallable>{{}};
}

template <typename F>
requires (!CallableObject<std::decay_t<F>>)
auto measure(F&&) {
    static_assert(detail::always_false<F>,
        "tempo::measure: this argument is not a callable object tempo can read.\n"
        TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE);
    return Metrics<detail::UnsupportedCallable>{{}, {}};
}
//-------------------------------------------------------------------

template <typename ClassType>
concept IsClass = std::is_class_v<ClassType>;

// Unconstrained, so that a wrong template argument produces the message below
// rather than "constraints not satisfied".
template <typename ClassType>
struct ConstructorProfiler{

    static_assert(IsClass<ClassType>,
        "tempo::ConstructorProfiler: the template argument must be a class or struct.\n"
        "  There is nothing to count for a fundamental type (int, double, char, a\n"
        "  pointer, an enum): those have no constructor to instrument, and\n"
        "  'int x = 5;' calls nothing.\n"
        "  Fix: name the class whose constructions you want counted --\n"
        "      tempo::ConstructorProfiler<MyClass> make;\n"
        "      MyClass obj = make(arg1, arg2);");

    inline static std::atomic<CallCount> obj_count{0};

    // A compile-time query: can ClassType be constructed from these arguments?
    // You can ask directly from your own code with
    // static_assert(Profiler::can_construct<int, int>).
    template <typename... Args>
    static constexpr bool can_construct = std::constructible_from<ClassType, Args...>;

    // The arguments are forwarded to the constructor: movable arguments are
    // moved, and because a prvalue is returned the object is constructed directly
    // in the caller's storage (guaranteed copy elision) -- so non-copyable and
    // non-movable types work too.
    //
    // WHEN does the counter increment? AFTER the object is constructed.
    // "return ClassType(...)" first initializes the caller's return object, and
    // LOCAL variables are destroyed only after that -- so the counter's
    // destructor runs once the constructor has finished successfully. If the
    // constructor throws, uncaught_exceptions() during unwinding differs from the
    // value on entry and the counter never increments.
    //
    // Binding the object to a named local and writing "obj_count++; return obj;"
    // would look clearer, but it would force a copy or move on the return and
    // make non-copyable, non-movable types like the one below fail to compile.
    template <typename... Args>
        requires std::constructible_from<ClassType, Args...>
    ClassType operator() (Args&&... args) const {
        [[maybe_unused]] const CountOnSuccess counter{};
        return ClassType(std::forward<Args>(args)...);
        };

    // Selected only when the overload above is NOT viable. Its sole job is to
    // produce an error message that says what happened, instead of "no match for
    // call". The return type is deliberately ClassType so that code writing
    // "auto p = make(...)" does not additionally get a "deduced type void is
    // incomplete" error -- one clear message is enough.
    template <typename... Args>
        requires (!std::constructible_from<ClassType, Args...>)
    ClassType operator() (Args&&...) const {
        static_assert(detail::always_false<Args...>,
            "tempo::ConstructorProfiler: ClassType cannot be constructed from these arguments. "
            "No matching constructor -- check the number and types of the arguments.");
    }

private:
    struct CountOnSuccess {
        int exceptions_on_entry = std::uncaught_exceptions();

        ~CountOnSuccess() {
            if (std::uncaught_exceptions() == exceptions_on_entry) {
                obj_count++;
            }
        }
    };
    };
 };
