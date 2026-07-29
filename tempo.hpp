#pragma once

// tempo — Copyright (c) 2026 Oktay Elibüyük
// Source-available, not open source. Use it in your own software freely,
// including commercially, and ship that software to anyone. Modify and extend it
// as you like. What you may NOT do without prior written permission is
// redistribute tempo itself, or your extended version of it, as a library for
// other developers to build with -- under this name or any other.
// See LICENSE for the full terms.

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

// Print a line-by-line report on every call? Set this to 0 and every cout call
// leaves the build; statistics are still collected, and you get a single summary
// from tempo::report(). Define it BEFORE the include.
#ifndef TEMPO_PRINT_ENABLED
#define TEMPO_PRINT_ENABLED 1
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

// A template-dependent "false", so the static_assert only fires when the
// template is actually instantiated. A plain "false" would make the compiler
// reject it on sight, before any instantiation.
template <typename...>
inline constexpr bool always_false = false;

// Decomposes the signature of a member function pointer. This is how we read the
// signature of lambdas and functors, via &F::operator().
template <typename MemberPointer>
struct MemberSignature;

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...)> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = false;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...) const> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = true;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...) noexcept> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = false;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...) const noexcept> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = true;
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
    unsigned int calls = 0;
    double total_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    bool has_samples = false;
    unsigned int max_depth = 0;
    unsigned int timed_calls = 0;

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

template<auto Func>
requires FunctionPointer<Func>
struct Function;

template <typename ret, typename... args, ret(*func_ptr)(args...)>
struct Function<func_ptr>{

    using  ReturnType = ret;
    using  ArgsType   = std::tuple<args...>;
    using  ClassType  = void;
    static constexpr bool is_member = false;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = false;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
    inline static std::atomic<unsigned int> call_count{0};

    // The parameters are forwarding references: an argument reaches func_ptr with
    // its own value category, without a copy in between. The std::invocable
    // constraint keeps the error at the call site rather than inside std::invoke.
    template <typename... CallArgs>
        requires std::invocable<decltype(func_ptr), CallArgs...>
    ReturnType operator()(CallArgs&&... call_args) const {
        call_count++;
        return std::invoke(func_ptr, std::forward<CallArgs>(call_args)...);
    }

};

template<auto MethodValue>
requires MethodPointer<MethodValue>
struct Method;

template <typename ClassName,typename ret, typename...args , ret(ClassName::*method)(args...)>
struct Method<method>{

    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    using ClassType = ClassName;
    static constexpr bool is_member = true;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = false;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) +  ... +  0);
    inline static std::atomic<unsigned int> call_count{0};

    // The instance is forwarded too: thanks to std::invoke, ClassName&,
    // ClassName*, std::reference_wrapper and smart pointers all work.
    template <typename Self, typename... CallArgs>
        requires std::invocable<decltype(method), Self, CallArgs...>
    ReturnType operator()(Self&& self, CallArgs&&... call_args) const {
        call_count++;
        return std::invoke(method, std::forward<Self>(self), std::forward<CallArgs>(call_args)...);
    }
};

template <typename ClassName,typename ret, typename...args , ret(ClassName::*method)(args...) const>
struct Method<method>{

    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    using ClassType = ClassName;
    static constexpr bool is_member = true;
    static constexpr bool is_const_member = true;
    static constexpr bool is_functor = false;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) +  ... +  0);
    inline static std::atomic<unsigned int> call_count{0};

    template <typename Self, typename... CallArgs>
        requires std::invocable<decltype(method), Self, CallArgs...>
    ReturnType operator()(Self&& self, CallArgs&&... call_args) const {
        call_count++;
        return std::invoke(method, std::forward<Self>(self), std::forward<CallArgs>(call_args)...);
    }
};

template<auto CallableValue>
requires SupportedCallable<CallableValue>
struct CallableImplementation;

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
requires SupportedCallable<CallableValue>
struct Callable : CallableImplementation<CallableValue>::Type {
    using CallableType = typename CallableImplementation<CallableValue>::Type;
    using CallableType::operator();
};

// Function and Method hold no state, so their template argument can be a pointer
// (an NTTP). Lambdas and functors, however, are OBJECTS: a capturing lambda can
// never be an NTTP. That is why Functor is templated on the type and carries the
// callable object itself inside.
template <typename F>
requires CallableObject<F>
struct Functor {

    using SignatureType = detail::MemberSignature<decltype(&F::operator())>;
    using ReturnType = typename SignatureType::ReturnType;
    using ArgsType   = typename SignatureType::ArgsType;
    using ClassType  = F;

    // is_member is false because the caller does not pass the instance
    // separately; the object lives inside the wrapper.
    static constexpr bool is_member = false;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = true;
    static constexpr bool is_const_callable = SignatureType::is_const;
    static constexpr auto arg_count = std::tuple_size_v<ArgsType>;
    static constexpr auto total_arg_size = SignatureType::total_arg_size;

    // The counter is tied to the type. Since every lambda EXPRESSION produces its
    // own unique closure type, this means a separate counter per lambda. Two
    // objects of the same type (two std::function<int(int)>, say) SHARE a counter.
    inline static std::atomic<unsigned int> call_count{0};

    // mutable: the operator() of a mutable lambda is not const, but the Profiler
    // and Metrics chain calls through a const path.
    mutable F target;

    template <typename... CallArgs>
        requires std::invocable<F&, CallArgs...>
    ReturnType operator()(CallArgs&&... call_args) const {
        call_count++;
        return std::invoke(target, std::forward<CallArgs>(call_args)...);
    }
};


// TMP is cleaner since C++20.
//
// Templated on the wrapper type (Callable<&f> or Functor<Lambda>), not on an
// NTTP: a lambda cannot be a template argument, but its type can.
template <typename WrapperType>
struct Profiler{

    using CallableType = WrapperType;
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


    ReturnType call_at(SourceLocation location, auto&&... args) const
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
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return call_at(SourceLocation::current(), std::forward<decltype(args)>(args)...);
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
template <auto CallableValue>
requires SupportedCallable<CallableValue>
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
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return static_cast<const Derived&>(*this).call_at(
            std::source_location::current(), std::forward<decltype(args)>(args)...);
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
    typename CallableType::ReturnType operator()(
        Args... args,
        std::source_location location = std::source_location::current()) const
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
struct Metrics : detail::CallOperator<Metrics<WrapperType>, WrapperType> {

    using CallableType = WrapperType;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType   = typename CallableType::ArgsType;
    using SourceLocation = std::source_location;

    // Metrics declares no operator() of its own -- it inherits exactly one, so
    // there is never an overload to resolve between. See detail::CallOperator.
    using CallOperatorBase = detail::CallOperator<Metrics<WrapperType>, WrapperType>;
    using CallOperatorBase::operator();
    // steady_clock, NOT high_resolution_clock. On libstdc++ high_resolution_clock
    // is an alias for system_clock (is_steady == false): if the wall clock is
    // stepped backwards by NTP, the difference between two Clock::now() calls
    // comes out negative or nonsensical. The only correct clock for measuring
    // durations is a monotonic one.
    using Clock = std::chrono::steady_clock;
    static_assert(Clock::is_steady, "tempo requires a monotonic clock to measure durations");
    using Duration = std::chrono::duration<double, std::milli>;

    static constexpr bool tracks_args = detail::ArgsAreStorable<ArgsType>::value;

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
    inline static unsigned int timed_calls = 0;

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
        unsigned int calls = 0;
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
        unsigned int timed_calls = 0;

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

    ReturnType call_at(SourceLocation location, auto&&... args) const
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

    StoredArgsType get_minimizers() const {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return min_args;
    }
    StoredArgsType get_maximizers() const {
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
template <auto CallableValue>
requires SupportedCallable<CallableValue>
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
//-------------------------------------------------------------------

template <typename ClassType>
concept IsClass = std::is_class_v<ClassType>;

template <typename ClassType>
requires IsClass<ClassType>
struct ConstructorProfiler{

    inline static std::atomic<unsigned int> obj_count{0};

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
