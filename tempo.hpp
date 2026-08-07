#pragma once

// tempo — Copyright (c) 2026 Oktay Elibüyük
// Released under the MIT License. See LICENSE for the full terms.

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
#include <array>
#include <chrono>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <span>
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

#ifndef TEMPO_PRINT_ENABLED
#define TEMPO_PRINT_ENABLED 0
#endif

#ifndef TEMPO_ENABLED
#define TEMPO_ENABLED 1
#endif

// How many of the slowest calls each metric keeps, arguments included.
// 0 stores none and costs nothing; the single fastest/slowest pair is kept
// either way. Like the other switches this changes a type, so it is ODR-
// sensitive: set it on the command line, not per translation unit.
#ifndef TEMPO_WORST_CALLS
#define TEMPO_WORST_CALLS 10
#endif

#if TEMPO_ENABLED
#define TEMPO_INSTRUMENT(function, alias, ...) \
    inline ::tempo::CallableMetrics<&function __VA_OPT__(,) __VA_ARGS__> alias{}
#else
#define TEMPO_INSTRUMENT(function, alias, ...) inline constexpr auto alias = &function
#endif

#ifndef TEMPO_COUNT_RECURSION
#define TEMPO_COUNT_RECURSION 0
#endif

#define TEMPO_TARGET(name) name##_tempo_target

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
#define TEMPO_CALLABLE_METRICS(callable, ...) \
    ::tempo::CallableMetrics<&callable __VA_OPT__(,) __VA_ARGS__>
#define TEMPO_PROFILE_CALL(profiler, ...) (profiler).call_at(::std::source_location::current() __VA_OPT__(,) __VA_ARGS__)

#define TEMPO_SCOPE_CAT_(a, b) a##b
#define TEMPO_SCOPE_ID_(a, b) TEMPO_SCOPE_CAT_(a, b)

#if TEMPO_ENABLED
#define TEMPO_SCOPE_AT_(label_expression)                                      \
    [[maybe_unused]] const ::tempo::scope_timing::ScopeTimer<decltype([] {})>  \
        TEMPO_SCOPE_ID_(tempo_scope_, __COUNTER__) { label_expression }
#define TEMPO_SCOPE()                                                          \
    TEMPO_SCOPE_AT_(::std::source_location::current().function_name())
#define TEMPO_SCOPE_NAMED(name) TEMPO_SCOPE_AT_(name)
#else
// Nothing is declared, so the statics never exist and the block is untouched.
// sizeof keeps a named argument counted as used without evaluating it.
#define TEMPO_SCOPE() ((void)0)
#define TEMPO_SCOPE_NAMED(name) ((void)sizeof(name))
#endif

namespace tempo{

using CallCount =  std::uint64_t;

namespace callable_traits {

template <auto Value>
concept FunctionPointer =
    std::is_pointer_v<decltype(Value)> &&
    std::is_function_v<std::remove_pointer_t<decltype(Value)>>;

template <auto Value>
concept MethodPointer = std::is_member_function_pointer_v<decltype(Value)>;

template <auto Value>
concept CallablePointer = FunctionPointer<Value> || MethodPointer<Value>;


template <typename F>
concept CallableObject =
    std::is_class_v<F> &&
    requires { &F::operator(); };

} // namespace callable_traits


namespace storage {


template <typename Tuple>
struct DecayedTuple;

template <typename... Ts>
struct DecayedTuple<std::tuple<Ts...>> {
    using Type = std::tuple<std::decay_t<Ts>...>;
};


template <typename Tuple>
struct ArgsAreStorable;

template <typename... Ts>
struct ArgsAreStorable<std::tuple<Ts...>>
    : std::bool_constant<(std::copy_constructible<std::decay_t<Ts>> && ...) &&
                         (std::default_initializable<std::decay_t<Ts>> && ...)> {};


template <typename Tuple>
struct ArgsAreNothrowStorable;

template <typename... Ts>
struct ArgsAreNothrowStorable<std::tuple<Ts...>>
    : std::bool_constant<
          (std::is_nothrow_default_constructible_v<std::decay_t<Ts>> && ...) &&
          (std::is_nothrow_copy_constructible_v<std::decay_t<Ts>>    && ...) &&
          (std::is_nothrow_copy_assignable_v<std::decay_t<Ts>>       && ...) &&
          (std::is_nothrow_move_assignable_v<std::decay_t<Ts>>       && ...)> {};

} // namespace storage


namespace errors {
template <typename...>
inline constexpr bool always_false = false;

template <auto...>
inline constexpr bool always_false_value = false;


struct UnsupportedArg {
    UnsupportedArg() = delete;
};


struct UnsupportedReturn {
    UnsupportedReturn() = default;


    template <typename T>
        requires (!std::same_as<std::decay_t<T>, UnsupportedReturn>)

    UnsupportedReturn(T&&) {}
    template <typename T>
    operator T() const;
};

struct UnsupportedCallable {
    using ReturnType = UnsupportedReturn;
    using ArgsType   = std::tuple<UnsupportedArg>;
    using ClassType  = void;


    static constexpr bool is_member = true;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = false;
    static constexpr bool is_const_callable = true;
    static constexpr bool is_noexcept = false;
    static constexpr std::size_t arg_count = 0;
    static constexpr std::size_t total_arg_size = 0;

    inline static std::atomic<CallCount> call_count{0};

    template <typename... CallArgs>
    UnsupportedReturn operator()(CallArgs&&...) const { return {}; }
};



#define TEMPO_C_VARIADIC_MESSAGE                                                   \
    "  A function declared with a trailing '...' (printf-like) cannot be wrapped\n"\
    "  faithfully: tempo would build a wrapper from the NAMED parameters only and\n"\
    "  quietly drop the '...', so calls passing variadic arguments would stop\n"    \
    "  compiling and the signature would no longer be the one you wrote.\n"         \
    "  Fix: wrap the calls you want measured in a lambda --\n"                      \
    "      auto m = tempo::measure([]{ return my_printf_like(3, 10, 20, 30); });"


#define TEMPO_NOT_A_WRAPPER_MESSAGE                                                \
    "  tempo::Metrics and tempo::Profiler wrap a tempo wrapper type, not a raw\n"  \
    "  function or lambda type. Use one of these instead:\n"                       \
    "      TEMPO_CALLABLE_METRICS(my_function)   // or TEMPO_INSTRUMENT(...)\n"     \
    "      auto m = tempo::measure(my_lambda);   // for lambdas and functors\n"     \
    "  Spelled out, the wrapped type is tempo::Callable<&my_function> for a\n"      \
    "  function and tempo::Functor<decltype(my_lambda)> for a lambda."


#define TEMPO_BAD_CALL_ARGUMENTS_MESSAGE                                           \
    "  Check the number, order and types of the arguments against the callable's\n"\
    "  own signature.\n"                                                           \
    "  For a MEMBER function the FIRST argument is the object itself, and the\n"    \
    "  method's own arguments follow it:\n"                                        \
    "      tempo::CallableMetrics<&Service::handle> m;\n"                          \
    "      m(service, 42);        // object first, then the method's arguments\n"   \
    "  The object may be passed as Service&, Service*, a std::reference_wrapper\n"  \
    "  or a smart pointer -- but it may not be left out."


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
    "      if constexpr (m.tracks_args) { ... m.slowest_args() ... }"


#define TEMPO_NO_WORST_CALLS_MESSAGE                                           \
    "tempo: worst_calls() is unavailable -- this metric keeps no ranking.\n"   \
    "  The number of slowest calls a metric retains is a template argument,\n" \
    "  and this one is 0, so nothing is ranked and there is nothing to\n"      \
    "  return. Either TEMPO_WORST_CALLS was defined as 0, or the count was\n"  \
    "  written out as 0 at the declaration.\n"                                 \
    "  Fix: give the metric a count of at least 1 --\n"                        \
    "      tempo::CallableMetrics<&run_query, 10> query;\n"                    \
    "      auto parse = tempo::measure<10>(my_lambda);\n"                      \
    "      TEMPO_INSTRUMENT(impl::render_page, render_page, 10);\n"            \
    "  slowest_args() and fastest_args() are unaffected -- the single\n"       \
    "  fastest and slowest call are kept whatever the count is."


#define TEMPO_UNREADABLE_FUNCTOR_MESSAGE                                       \
    "tempo: this callable object's operator() has a shape tempo cannot read.\n"\
    "  tempo supports a plain operator(), optionally const and/or noexcept.\n" \
    "  It does NOT support a ref-qualified operator() (declared with a trailing\n"\
    "  '&' or '&&'), a volatile operator(), or a C-style variadic one ('...').\n"\
    "  Fix: declare the functor's operator() without the ref-qualifier, or wrap\n"\
    "  the object in a lambda with concrete parameter types and pass that to\n" \
    "  tempo::measure(...)."


#define TEMPO_UNSUPPORTED_FUNCTION_MESSAGE                                     \
    "tempo: this function's type is not supported.\n"                          \
    "  tempo matches function pointers of the form  ret(*)(args...), with or\n"\
    "  without 'noexcept'.\n"                                                  \
    TEMPO_C_VARIADIC_MESSAGE


#define TEMPO_UNSUPPORTED_METHOD_MESSAGE                                       \
    "tempo: this member function's type is not supported.\n"                   \
    "  tempo matches member function pointers of the form  ret(Class::*)(args...),\n"\
    "  their const version, and either of those declared 'noexcept'.\n"        \
    "  A ref-qualified member function (declared with a trailing '&' or '&&'), a\n"\
    "  volatile one, or a C-style variadic one ('...') does not match.\n"      \
    "  Fix: wrap the call in a lambda that captures the object and measure that --\n"\
    "      auto m = tempo::measure([&obj](int a){ return obj.my_method(a); });"


#define TEMPO_NOT_A_CALLABLE_POINTER_MESSAGE                                   \
    "tempo: the template argument is not a function or member function pointer.\n"\
    "  TEMPO_INSTRUMENT, TEMPO_CALLABLE_METRICS and TEMPO_CALLABLE_PROFILER take\n"\
    "  the ADDRESS OF A FUNCTION:\n"                                           \
    "      TEMPO_INSTRUMENT(impl::my_function, my_function);\n"                 \
    "  If you passed a lambda, a functor or a std::function, use a factory\n"   \
    "  instead -- those are objects, not function pointers, and an object\n"    \
    "  cannot be a template argument:\n"                                       \
    "      auto m = tempo::measure(my_lambda);\n"                              \
    "      m(arg1, arg2);"


#define TEMPO_NOT_A_CLASS_MESSAGE                                              \
    "tempo::ConstructorProfiler: the template argument must be a class or struct.\n"\
    "  There is nothing to count for a fundamental type (int, double, char, a\n"\
    "  pointer, an enum): those have no constructor to instrument, and\n"      \
    "  'int x = 5;' calls nothing.\n"                                          \
    "  Fix: name the class whose constructions you want counted --\n"          \
    "      tempo::ConstructorProfiler<MyClass> make;\n"                        \
    "      MyClass obj = make(arg1, arg2);"


#define TEMPO_BAD_CONSTRUCTOR_ARGUMENTS_MESSAGE                                \
    "tempo::ConstructorProfiler: ClassType cannot be constructed from these arguments. "\
    "No matching constructor -- check the number and types of the arguments."


struct UnsupportedSignature {
    using ReturnType = UnsupportedReturn;
    using ArgsType   = std::tuple<UnsupportedArg>;
    static constexpr bool is_const = true;
    static constexpr bool is_noexcept = false;
    static constexpr std::size_t total_arg_size = 0;
};

} // namespace errors

namespace report {


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


struct Row {
    std::string name;
    CallCount calls = 0;
    double total_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    bool has_samples = false;
    unsigned int max_depth = 0;
    CallCount timed_calls = 0;


    double average_ms() const { return timed_calls ? total_ms / timed_calls : 0.0; }
};

using RowFetcher = Row (*)();
using Resetter = void (*)();

struct Registry {
    std::mutex mutex;
    std::vector<RowFetcher> fetchers;
    std::vector<Resetter> resetters;
};


inline Registry& registry() {
    static Registry instance;
    return instance;
}

inline void add_metric(RowFetcher fetcher, Resetter resetter) {
    Registry& reg = registry();
    const std::lock_guard<std::mutex> guard{reg.mutex};
    reg.fetchers.push_back(fetcher);
    reg.resetters.push_back(resetter);
}

// Every registered metric's row, in registration order and including the ones
// that were never called. print() is this plus filtering, sorting and layout;
// anything that wants the numbers rather than the table should read this, since
// the table's spelling is compiler-dependent and not meant to be parsed.
inline std::vector<Row> collect() {
    std::vector<Row> rows;
    Registry& reg = registry();
    const std::lock_guard<std::mutex> guard{reg.mutex};
    rows.reserve(reg.fetchers.size());
    for (const RowFetcher fetch : reg.fetchers) { rows.push_back(fetch()); }
    return rows;
}

inline void print(std::ostream& out = std::cout) {
    std::vector<Row> rows = collect();

    std::erase_if(rows, [](const Row& row) { return row.calls == 0; });
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


    const bool show_depth = std::ranges::any_of(
        rows, [](const Row& row) { return row.max_depth > 1; });
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

inline void reset_all() {
    Registry& reg = registry();
    const std::lock_guard<std::mutex> guard{reg.mutex};
    for (const Resetter reset : reg.resetters) { reset(); }
}

inline void at_exit(std::ostream& out = std::cout) {
    struct AtExit {
        std::ostream* stream;
        ~AtExit() { print(*stream); }
    };
    static AtExit guard{&out};   // its destructor runs at program exit
    (void)guard;
}

} // namespace report


namespace scope_timing {

// The block timer behind TEMPO_SCOPE. Tag is a closure type minted by the
// macro, unique per expansion, so every scope gets its own statics; the object
// on the stack carries only what one entry needs and the numbers outlive it.
//
// Nothing here is meant to be spelled by hand -- the tag has no name you can
// write. Read the results through tempo::report.
template <typename Tag>
struct ScopeTimer {
    using Clock = std::chrono::steady_clock;
    static_assert(Clock::is_steady, "tempo requires a monotonic clock to measure durations");
    using Duration = std::chrono::duration<double, std::milli>;

    inline static std::atomic<CallCount> entry_count{0};

private:
    inline static std::mutex stats_mutex;
    inline static bool has_samples = false;
    inline static CallCount timed_entries = 0;
    inline static Duration total_duration{0};
    inline static Duration min_duration{0};
    inline static Duration max_duration{0};
    inline static unsigned int max_depth = 0;

    // Always the same pointer for a given tag -- the macro passes one literal or
    // one source_location per site -- but written on every entry, so atomic
    // rather than a plain store several threads race on.
    inline static std::atomic<const char*> label{nullptr};

    inline static thread_local unsigned int depth = 0;
    inline static thread_local unsigned int peak_depth = 0;

    // A scope re-entered by recursion must not sum intervals that contain one
    // another, so only the outermost entry is timed. Every entry is counted.
    static bool enter_depth() {
        const unsigned int current = ++depth;
        if (current == 1) { peak_depth = 1; }
        else if (current > peak_depth) { peak_depth = current; }
        return current == 1;
    }

public:
    struct Snapshot {
        CallCount entries = 0;
        CallCount timed_entries = 0;
        Duration total_duration{0};
        Duration min_duration{0};
        Duration max_duration{0};
        unsigned int max_depth = 0;
        bool has_samples = false;

        double average_ms() const {
            return timed_entries ? total_duration.count() / timed_entries : 0.0;
        }
    };

    static Snapshot snapshot() {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return Snapshot{entry_count.load(std::memory_order_relaxed),
                        timed_entries, total_duration, min_duration,
                        max_duration, max_depth,       has_samples};
    }

    static void reset() {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        entry_count.store(0, std::memory_order_relaxed);
        has_samples = false;
        timed_entries = 0;
        total_duration = Duration{0};
        min_duration = Duration{0};
        max_duration = Duration{0};
        max_depth = 0;
    }

    static void ensure_registered() {
        static const bool once = [] {
            report::add_metric(
                [] {
                    const Snapshot state = snapshot();
                    const char* name = label.load(std::memory_order_relaxed);
                    return report::Row{name ? std::string{name} : std::string{"(scope)"},
                                       state.entries,
                                       state.total_duration.count(),
                                       state.min_duration.count(),
                                       state.max_duration.count(),
                                       state.has_samples,
                                       state.max_depth,
                                       state.timed_entries};
                },
                [] { reset(); });
            return true;
        }();
        (void)once;
    }

    explicit ScopeTimer(const char* name)
        : outermost(enter_depth()), exceptions_on_entry(std::uncaught_exceptions()) {
        label.store(name, std::memory_order_relaxed);
        entry_count.fetch_add(1, std::memory_order_relaxed);
        ensure_registered();
        // Read last, so registering and counting land outside the measurement.
        if (outermost) { start = Clock::now(); }
    }

    // A scope timer names a region of code; copying or moving one would mean a
    // second end for a single beginning.
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

    ~ScopeTimer() {
        // GCC reads folding a local duration into a static as a dangling store.
        // Same false positive, same workaround, as Metrics::RecordOnExit below.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
        // First thing, for the same reason start is read last.
        const Clock::time_point stop = outermost ? Clock::now() : Clock::time_point{};

        --depth;

        // Left by a throw: the region did not finish, so it is not a sample.
        if (std::uncaught_exceptions() != exceptions_on_entry) { return; }
        if (!outermost) { return; }

        const Duration duration = stop - start;

        // Taken only after the clock has stopped, so it never inflates a
        // measurement and never serialises the code being timed.
        const std::lock_guard<std::mutex> guard{stats_mutex};

        if (peak_depth > max_depth) { max_depth = peak_depth; }

        ++timed_entries;
        total_duration += duration;

        const bool is_new_max = !has_samples || duration > max_duration;
        const bool is_new_min = !has_samples || duration < min_duration;
        if (is_new_max) { max_duration = duration; }
        if (is_new_min) { min_duration = duration; }
        has_samples = true;

#if TEMPO_PRINT_ENABLED
        const char* name = label.load(std::memory_order_relaxed);
        std::cout << "[ScopeTimer] " << (name ? name : "(scope)")
                  << " took: " << duration.count() << " ms\n";
        std::cout << "[ScopeTimer] Entries: "
                  << entry_count.load(std::memory_order_relaxed) << "\n";
        std::cout << "[ScopeTimer] Total time spent: " << total_duration.count() << " ms\n";
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    }

private:
    const bool outermost;
    const int exceptions_on_entry;
    Clock::time_point start{};
};

} // namespace scope_timing


namespace wrapper {


template <typename W>
concept TempoWrapper = requires {
    typename W::ReturnType;
    typename W::ArgsType;
    { W::is_member } -> std::convertible_to<bool>;
};

template <typename W>
struct WrapperOrStandIn {
    using Type = errors::UnsupportedCallable;
};

template <typename W>
requires TempoWrapper<W>
struct WrapperOrStandIn<W> {
    using Type = W;
};

} // namespace wrapper

namespace function_binding {


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


    template <typename... CallArgs>
        requires std::invocable<decltype(func_ptr), CallArgs...>
    ReturnType operator()(CallArgs&&... call_args) const noexcept(Noexcept) {
        call_count++;
        return std::invoke(func_ptr, std::forward<CallArgs>(call_args)...);
    }
};


template <typename Signature, auto func_ptr>
struct FunctionImpl : errors::UnsupportedCallable {
    static_assert(errors::always_false_value<func_ptr>,
        TEMPO_UNSUPPORTED_FUNCTION_MESSAGE);
};

template <typename ret, typename... args, auto func_ptr>
struct FunctionImpl<ret(args...), func_ptr>
    : FunctionBody<func_ptr, false, ret, args...> {};

template <typename ret, typename... args, auto func_ptr>
struct FunctionImpl<ret(args...) noexcept, func_ptr>
    : FunctionBody<func_ptr, true, ret, args...> {};

} // namespace function_binding

template<auto Func>
requires callable_traits::FunctionPointer<Func>
struct Function : function_binding::FunctionImpl<std::remove_pointer_t<decltype(Func)>, Func> {};

namespace method_binding {


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

    template <typename Self, typename... CallArgs>
        requires std::invocable<decltype(method), Self, CallArgs...>
    ReturnType operator()(Self&& self, CallArgs&&... call_args) const noexcept(Noexcept) {
        call_count++;
        return std::invoke(method, std::forward<Self>(self), std::forward<CallArgs>(call_args)...);
    }
};

template <typename Signature, auto method>
struct MethodImpl : errors::UnsupportedCallable {
    static_assert(errors::always_false_value<method>,
        TEMPO_UNSUPPORTED_METHOD_MESSAGE);
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

} // namespace method_binding

template<auto MethodValue>
requires callable_traits::MethodPointer<MethodValue>
struct Method : method_binding::MethodImpl<decltype(MethodValue), MethodValue> {};


namespace callable_matcher {

template<auto CallableValue>
struct Implementation {
    using Type = errors::UnsupportedCallable;
};

template<auto CallableValue>
requires callable_traits::FunctionPointer<CallableValue>
struct Implementation<CallableValue> {
    using Type = Function<CallableValue>;
};

template<auto CallableValue>
requires callable_traits::MethodPointer<CallableValue>
struct Implementation<CallableValue> {
    using Type = Method<CallableValue>;
};

} // namespace callable_matcher

template<auto CallableValue>
struct Callable : callable_matcher::Implementation<CallableValue>::Type {

    static_assert(callable_traits::CallablePointer<CallableValue>,
        TEMPO_NOT_A_CALLABLE_POINTER_MESSAGE);


    using CallableType = typename callable_matcher::Implementation<CallableValue>::Type;
    using CallableType::operator();
};


namespace functor_binding {

template <bool Noexcept, bool Const, typename ret, typename... args>
struct FunctorBody {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = Const;
    static constexpr bool is_noexcept = Noexcept;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};


template <typename CallOperatorPointer>
struct FunctorImpl : errors::UnsupportedSignature {
    static_assert(errors::always_false<CallOperatorPointer>,
        TEMPO_UNREADABLE_FUNCTOR_MESSAGE);
};

template <typename Owner, typename ret, typename... args>
struct FunctorImpl<ret (Owner::*)(args...)>
    : FunctorBody<false, false, ret, args...> {};

template <typename Owner, typename ret, typename... args>
struct FunctorImpl<ret (Owner::*)(args...) const>
    : FunctorBody<false, true, ret, args...> {};

template <typename Owner, typename ret, typename... args>
struct FunctorImpl<ret (Owner::*)(args...) noexcept>
    : FunctorBody<true, false, ret, args...> {};

template <typename Owner, typename ret, typename... args>
struct FunctorImpl<ret (Owner::*)(args...) const noexcept>
    : FunctorBody<true, true, ret, args...> {};


template <typename F>
struct Implementation {
    using Type = errors::UnsupportedSignature;
};

template <typename F>
requires callable_traits::CallableObject<F>
struct Implementation<F> {
    using Type = FunctorImpl<decltype(&F::operator())>;
};

} // namespace functor_binding

template <typename F>
struct Functor {

    static_assert(callable_traits::CallableObject<F>,
        "tempo: this is not a callable object tempo can read.\n"
        TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE);

    using ImplType   = typename functor_binding::Implementation<F>::Type;
    using ReturnType = typename ImplType::ReturnType;
    using ArgsType   = typename ImplType::ArgsType;
    using ClassType  = F;

    static constexpr bool is_member = false;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = true;
    static constexpr bool is_const_callable = ImplType::is_const;
    static constexpr bool is_noexcept = ImplType::is_noexcept;
    static constexpr auto arg_count = std::tuple_size_v<ArgsType>;
    static constexpr auto total_arg_size = ImplType::total_arg_size;

    inline static std::atomic<CallCount> call_count{0};

    mutable F target;

    template <typename... CallArgs>
        requires std::invocable<F&, CallArgs...>
    ReturnType operator()(CallArgs&&... call_args) const noexcept(is_noexcept) {
        call_count++;

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



namespace wrapper {


template <typename W>
inline constexpr bool is_tempo_wrapper_template = false;

template <auto V>
inline constexpr bool is_tempo_wrapper_template<Callable<V>> = true;
template <auto V>
    requires callable_traits::FunctionPointer<V>
inline constexpr bool is_tempo_wrapper_template<Function<V>> = true;
template <auto V>
    requires callable_traits::MethodPointer<V>
inline constexpr bool is_tempo_wrapper_template<Method<V>> = true;
template <typename F>
inline constexpr bool is_tempo_wrapper_template<Functor<F>> = true;
template <>
inline constexpr bool is_tempo_wrapper_template<errors::UnsupportedCallable> = true;

} // namespace wrapper

template <typename WrapperType>
struct Profiler{

    static_assert(wrapper::TempoWrapper<WrapperType> ||
                  wrapper::is_tempo_wrapper_template<WrapperType>,
        "tempo::Profiler: this is not a tempo wrapper type.\n"
        TEMPO_NOT_A_WRAPPER_MESSAGE);


    using CallableType = typename wrapper::WrapperOrStandIn<WrapperType>::Type;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType = typename CallableType::ArgsType;

    using SourceLocation = std::source_location;


    inline static std::mutex state_mutex;
    inline static SourceLocation last_call_location{};

    static SourceLocation get_last_call_location() {
        const std::lock_guard<std::mutex> guard{state_mutex};
        return last_call_location;
    }

    CallableType callable;


    static constexpr bool is_noexcept = CallableType::is_noexcept;


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

        // Keep post-call work in a destructor so the call expression returns directly.
        [[maybe_unused]] const ReportOnExit report{};
        return callable(std::forward<decltype(args)>(args)...);
    }

    ReturnType operator()(auto&&... args) const
        noexcept(is_noexcept)
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return call_at(SourceLocation::current(), std::forward<decltype(args)>(args)...);
    }

    ReturnType operator()(auto&&... args) const
        requires (!std::invocable<const CallableType&, decltype(args)...>)
    {
        static_assert(errors::always_false<decltype(args)...>,
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


template <auto CallableValue>
using CallableProfiler = Profiler<Callable<CallableValue>>;
namespace call_operators {


template <typename Derived, typename CallableType>
struct VariadicCall {
    typename CallableType::ReturnType operator()(auto&&... args) const
        noexcept(CallableType::is_noexcept)
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return static_cast<const Derived&>(*this).call_at(
            std::source_location::current(), std::forward<decltype(args)>(args)...);
    }
};


template <typename Derived, typename CallableType, typename ArgsTuple>
struct FixedSignatureCall;

template <typename Derived, typename CallableType, typename... Args>
struct FixedSignatureCall<Derived, CallableType, std::tuple<Args...>> {

    typename CallableType::ReturnType operator()(
        Args... args,
        std::source_location location = std::source_location::current()) const
        noexcept(CallableType::is_noexcept)
    {
        return static_cast<const Derived&>(*this).call_at(
            location, static_cast<Args&&>(args)...);
    }


    typename CallableType::ReturnType operator()(auto&&... args) const
        requires (!std::invocable<const CallableType&, decltype(args)...>)
    {
        static_assert(errors::always_false<decltype(args)...>,
            "tempo: this callable cannot be invoked with the arguments you passed.\n"
            TEMPO_BAD_CALL_ARGUMENTS_MESSAGE);
    }
};


template <typename Derived, typename CallableType, typename ArgsTuple>
struct MemberCall;

template <typename Derived, typename CallableType, typename... Args>
struct MemberCall<Derived, CallableType, std::tuple<Args...>> {

    template <typename Self>
        requires std::invocable<const CallableType&, Self, Args...>
    typename CallableType::ReturnType operator()(
        Self&& self, Args... args,
        std::source_location location = std::source_location::current()) const
        noexcept(CallableType::is_noexcept)
    {
        return static_cast<const Derived&>(*this).call_at(
            location, std::forward<Self>(self), static_cast<Args&&>(args)...);
    }


    typename CallableType::ReturnType operator()(auto&&... args) const
        requires (!std::invocable<const CallableType&, decltype(args)...>)
    {
        static_assert(errors::always_false<decltype(args)...>,
            "tempo: this callable cannot be invoked with the arguments you passed.\n"
            TEMPO_BAD_CALL_ARGUMENTS_MESSAGE);
    }
};

template <typename Derived, typename CallableType>
using SignatureCall = std::conditional_t<
    CallableType::is_member,
    MemberCall<Derived, CallableType, typename CallableType::ArgsType>,
    FixedSignatureCall<Derived, CallableType, typename CallableType::ArgsType>>;

template <typename Derived, typename CallableType>
using CallOperator = std::conditional_t<
    std::derived_from<CallableType, errors::UnsupportedCallable>,
    VariadicCall<Derived, CallableType>,
    SignatureCall<Derived, CallableType>>;

} // namespace call_operators


template <typename WrapperType, std::size_t WorstCalls = TEMPO_WORST_CALLS>
struct Metrics : call_operators::CallOperator<Metrics<WrapperType, WorstCalls>,
                                      typename wrapper::WrapperOrStandIn<WrapperType>::Type> {

    static_assert(wrapper::TempoWrapper<WrapperType> ||
                  wrapper::is_tempo_wrapper_template<WrapperType>,
        "tempo::Metrics: this is not a tempo wrapper type.\n"
        TEMPO_NOT_A_WRAPPER_MESSAGE);


    using CallableType = typename wrapper::WrapperOrStandIn<WrapperType>::Type;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType   = typename CallableType::ArgsType;
    using SourceLocation = std::source_location;

    using CallOperatorBase =
        call_operators::CallOperator<Metrics<WrapperType, WorstCalls>, CallableType>;
    using CallOperatorBase::operator();

    using Clock = std::chrono::steady_clock;
    static_assert(Clock::is_steady, "tempo requires a monotonic clock to measure durations");
    using Duration = std::chrono::duration<double, std::milli>;

    static constexpr bool is_noexcept = CallableType::is_noexcept;


    static constexpr bool tracks_args =
        storage::ArgsAreStorable<ArgsType>::value &&
        (!is_noexcept || storage::ArgsAreNothrowStorable<ArgsType>::value);

    using StoredArgsType = std::conditional_t<
        tracks_args,
        typename storage::DecayedTuple<ArgsType>::Type,
        std::tuple<>>;

    // How many of the slowest calls this metric ranks. Independent of
    // tracks_args: with capture off the ranking still carries durations and
    // call sites, and `args` is the empty tuple StoredArgsType already is.
    static constexpr std::size_t worst_capacity = WorstCalls;
    static constexpr bool ranks_worst = WorstCalls > 0;

    // One retained call. Ranked by duration, slowest first.
    struct WorstCall {
        Duration duration{0};
        StoredArgsType args{};
        SourceLocation location{};
    };

    // Ranking overwrites entries, so a noexcept callable must be able to copy
    // and shift one without throwing. ArgsAreNothrowStorable already demands
    // exactly that of every parameter; this proves it survived the wrapping.
    static_assert(!is_noexcept || !ranks_worst ||
                      (std::is_nothrow_copy_assignable_v<WorstCall> &&
                       std::is_nothrow_move_assignable_v<WorstCall>),
        "tempo: internal -- ranking the slowest calls of a noexcept callable "
        "must not throw.");

    inline static auto& call_count = CallableType::call_count;

private:

    inline static std::mutex stats_mutex;
    inline static bool has_samples = false;

    // Sorted descending over [0, worst_size). A zero-length array is a valid
    // std::array, so an arity of 0 or a capacity of 0 both stay well-formed.
    inline static std::array<WorstCall, WorstCalls> worst{};
    inline static std::size_t worst_size = 0;


    inline static CallCount timed_calls = 0;

    inline static Duration total_duration{0};
    inline static Duration max_duration{0};
    inline static Duration min_duration{0};
    inline static StoredArgsType min_args{};
    inline static StoredArgsType max_args{};
    inline static SourceLocation last_call_location{};
    inline static unsigned int max_depth = 0;


    inline static thread_local unsigned int depth = 0;


    inline static thread_local unsigned int peak_depth = 0;

    static bool enter_depth() {
        const unsigned int current = ++depth;
        // Reset on entry so an unwound recursive call cannot leak its peak.
        if (current == 1) { peak_depth = 1; }
        else if (current > peak_depth) { peak_depth = current; }
        return current == 1;
    }

public:
    // One locked view, so totals, extremes and arguments describe the same state.
    struct Snapshot {
        CallCount calls = 0;
        Duration total_duration{0};
        Duration min_duration{0};
        Duration max_duration{0};
        StoredArgsType min_args{};
        StoredArgsType max_args{};
        SourceLocation last_call_location{};
        bool has_samples = false;

        unsigned int max_depth = 0;

        CallCount timed_calls = 0;

        // Read under the same lock as everything above, so the ranking and the
        // totals describe the same moment.
        std::array<WorstCall, WorstCalls> worst{};
        std::size_t worst_size = 0;

        double average_ms() const {
            return timed_calls ? total_duration.count() / timed_calls : 0.0;
        }

        // Only the entries actually filled, slowest first.
        std::span<const WorstCall> worst_calls() const {
            return std::span<const WorstCall>{worst.data(), worst_size};
        }
    };

    static Snapshot snapshot() {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return Snapshot{call_count.load(std::memory_order_relaxed),
                        total_duration,    min_duration,      max_duration,
                        min_args,          slowest_args_ref(), last_call_location,
                        has_samples,       max_depth,         timed_calls,
                        worst,             worst_size};
    }

    static SourceLocation get_last_call_location() {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return last_call_location;
    }


    static unsigned int current_depth() { return depth; }


    CallableType callable;


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
        worst = {};
        worst_size = 0;
    }


    static void ensure_registered() {
        static const bool once = [] {
            report::add_metric(
                [] {
                    const Snapshot state = snapshot();
                    return report::Row{std::string{report::type_name<CallableType>()},
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
        noexcept(is_noexcept)
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        ensure_registered();


        StoredArgsType snapshot = make_args_snapshot(args...);

        [[maybe_unused]] const RecordOnExit record{location, snapshot};
        return callable(std::forward<decltype(args)>(args)...);
    }


    ReturnType call_at(SourceLocation, auto&&... args) const
        requires (!std::invocable<const CallableType&, decltype(args)...>)
    {
        static_assert(errors::always_false<decltype(args)...>,
            "tempo: call_at -- this callable cannot be invoked with these arguments.\n"
            TEMPO_BAD_CALL_ARGUMENTS_MESSAGE);
    }

    StoredArgsType fastest_args() const {
        static_assert(tracks_args,
            "tempo: fastest_args() is unavailable -- this callable's arguments are not stored.\n"
            TEMPO_ARGS_NOT_STORED_MESSAGE);
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return min_args;
    }
    StoredArgsType slowest_args() const {
        static_assert(tracks_args,
            "tempo: slowest_args() is unavailable -- this callable's arguments are not stored.\n"
            TEMPO_ARGS_NOT_STORED_MESSAGE);
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return slowest_args_ref();
    }

    // The slowest calls seen, slowest first. Shorter than worst_capacity until
    // that many have been timed; empty before the first one.
    static std::vector<WorstCall> worst_calls() {
        static_assert(ranks_worst, TEMPO_NO_WORST_CALLS_MESSAGE);
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return std::vector<WorstCall>{worst.begin(), worst.begin() + worst_size};
    }

private:

    // Claims this call's place in the ranking and returns the slot it earned,
    // or nullptr if it was not slow enough. The arguments are left to the
    // caller so it can hand the single snapshot to its last consumer by move.
    //
    // Called under stats_mutex. Ranking is rare once the table fills, so the
    // common path is the one comparison against the tail that rejects.
    static WorstCall* rank_worst(Duration duration, SourceLocation location) noexcept {
        if constexpr (!ranks_worst) {
            return nullptr;
        }
        else {
            if (worst_size == worst_capacity &&
                !(duration > worst[worst_capacity - 1].duration)) {
                return nullptr;
            }

            // Displace the tail once full; otherwise grow into the next slot.
            std::size_t slot = worst_size < worst_capacity ? worst_size++
                                                           : worst_capacity - 1;
            for (; slot > 0 && duration > worst[slot - 1].duration; --slot) {
                worst[slot] = std::move(worst[slot - 1]);
            }

            worst[slot].duration = duration;
            worst[slot].location = location;
            return &worst[slot];
        }
    }

    // The slowest call's arguments. With a ranking that is just its head --
    // a new maximum always sorts to slot 0 -- so nothing is stored twice.
    // Called under stats_mutex.
    static const StoredArgsType& slowest_args_ref() noexcept {
        if constexpr (ranks_worst) {
            return worst_size > 0 ? worst[0].args : max_args;   // empty before the first call
        }
        else {
            return max_args;
        }
    }

    template <typename Instance, typename... MethodArgs>
    static StoredArgsType make_args_snapshot_without_instance(const Instance&, const MethodArgs&... arg) {
        return StoredArgsType{arg...};
    }

    struct RecordOnExit {
        SourceLocation location;
        StoredArgsType& snapshot;
        int exceptions_on_entry = std::uncaught_exceptions();

        bool outermost = enter_depth();

        // Inner recursive calls are counted but not timed.
        Clock::time_point start = outermost ? Clock::now() : Clock::time_point{};

        ~RecordOnExit() {

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif

            Duration duration{0};
            if (outermost) { duration = Clock::now() - start; }


            --depth;

            if (std::uncaught_exceptions() != exceptions_on_entry) {
                return; // the call threw, do not record a half-finished duration
            }


            if (!outermost) { return; }

            const std::lock_guard<std::mutex> guard{stats_mutex};


            if (peak_depth > max_depth) { max_depth = peak_depth; }

            last_call_location = location;
            ++timed_calls;
            total_duration += duration;

            const bool is_new_max = !has_samples || duration > max_duration;
            const bool is_new_min = !has_samples || duration < min_duration;

            if (is_new_max) { max_duration = duration; }
            if (is_new_min) { min_duration = duration; }
            has_samples = true;

            WorstCall* const ranked = rank_worst(duration, location);

            // One snapshot, up to two homes: everything but the last consumer
            // copies. With a ranking the slowest call lives in slot 0, so
            // max_args is not a second place to put it.
            if constexpr (tracks_args) {
                if constexpr (ranks_worst) {
                    if (ranked != nullptr && is_new_min) {
                        ranked->args = snapshot;
                        min_args = std::move(snapshot);
                    }
                    else if (ranked != nullptr) { ranked->args = std::move(snapshot); }
                    else if (is_new_min)        { min_args = std::move(snapshot); }
                }
                else {
                    if (is_new_max && is_new_min) {
                        max_args = snapshot;
                        min_args = std::move(snapshot);
                    }
                    else if (is_new_max) { max_args = std::move(snapshot); }
                    else if (is_new_min) { min_args = std::move(snapshot); }
                }
            }
            else {
                (void)ranked;
            }

#if TEMPO_PRINT_ENABLED

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


template <auto CallableValue, std::size_t WorstCalls = TEMPO_WORST_CALLS>
using CallableMetrics = Metrics<Callable<CallableValue>, WorstCalls>;


template <typename F>
requires callable_traits::CallableObject<std::decay_t<F>>
auto wrap(F&& target) {
    return Functor<std::decay_t<F>>{std::forward<F>(target)};
}

template <typename F>
requires callable_traits::CallableObject<std::decay_t<F>>
auto profile(F&& target) {
    return Profiler<Functor<std::decay_t<F>>>{wrap(std::forward<F>(target))};
}

// The count comes first so it can be written without naming the closure type:
//     auto parse = tempo::measure<25>(my_lambda);
template <std::size_t WorstCalls = TEMPO_WORST_CALLS, typename F>
requires callable_traits::CallableObject<std::decay_t<F>>
auto measure(F&& target) {
    return Metrics<Functor<std::decay_t<F>>, WorstCalls>{{}, wrap(std::forward<F>(target))};
}


template <typename F>
requires (!callable_traits::CallableObject<std::decay_t<F>>)
auto wrap(F&&) {
    static_assert(errors::always_false<F>,
        "tempo::wrap: this argument is not a callable object tempo can read.\n"
        TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE);
    return errors::UnsupportedCallable{};
}

template <typename F>
requires (!callable_traits::CallableObject<std::decay_t<F>>)
auto profile(F&&) {
    static_assert(errors::always_false<F>,
        "tempo::profile: this argument is not a callable object tempo can read.\n"
        TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE);
    return Profiler<errors::UnsupportedCallable>{{}};
}

template <std::size_t WorstCalls = TEMPO_WORST_CALLS, typename F>
requires (!callable_traits::CallableObject<std::decay_t<F>>)
auto measure(F&&) {
    static_assert(errors::always_false<F>,
        "tempo::measure: this argument is not a callable object tempo can read.\n"
        TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE);
    return Metrics<errors::UnsupportedCallable, WorstCalls>{{}, {}};
}
namespace construction {

template <typename ClassType>
concept Class = std::is_class_v<ClassType>;

} // namespace construction

template <typename ClassType>
struct ConstructorProfiler{

    static_assert(construction::Class<ClassType>,
        TEMPO_NOT_A_CLASS_MESSAGE);

    inline static std::atomic<CallCount> obj_count{0};

    template <typename... Args>
    static constexpr bool can_construct = std::constructible_from<ClassType, Args...>;


    template <typename... Args>
        requires std::constructible_from<ClassType, Args...>
    ClassType operator() (Args&&... args) const {
        [[maybe_unused]] const CountOnSuccess counter{};
        return ClassType(std::forward<Args>(args)...);
        };


    template <typename... Args>
        requires (!std::constructible_from<ClassType, Args...>)
    ClassType operator() (Args&&...) const {
        static_assert(errors::always_false<Args...>,
            TEMPO_BAD_CONSTRUCTOR_ARGUMENTS_MESSAGE);
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


//#undef so their scope is only within this heaeder
#undef TEMPO_C_VARIADIC_MESSAGE
#undef TEMPO_NOT_A_WRAPPER_MESSAGE
#undef TEMPO_BAD_CALL_ARGUMENTS_MESSAGE
#undef TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE
#undef TEMPO_ARGS_NOT_STORED_MESSAGE
#undef TEMPO_UNREADABLE_FUNCTOR_MESSAGE
#undef TEMPO_UNSUPPORTED_FUNCTION_MESSAGE
#undef TEMPO_UNSUPPORTED_METHOD_MESSAGE
#undef TEMPO_NOT_A_CALLABLE_POINTER_MESSAGE
#undef TEMPO_NOT_A_CLASS_MESSAGE
#undef TEMPO_BAD_CONSTRUCTOR_ARGUMENTS_MESSAGE
