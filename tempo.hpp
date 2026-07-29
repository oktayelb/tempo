#pragma once

// tempo — Copyright (c) 2026 Oktay Elibüyük
// Source-available, not open source. You may use, study, modify and extend this
// software; you may NOT distribute it without prior written permission, either
// standalone or embedded in another product, in source or compiled form.
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

    double average_ms() const { return calls ? total_ms / calls : 0.0; }
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

    out << std::left << std::setw(static_cast<int>(width)) << "callable"
        << std::right
        << std::setw(8)  << "calls"
        << std::setw(12) << "total ms"
        << std::setw(12) << "avg ms"
        << std::setw(12) << "min ms"
        << std::setw(12) << "max ms" << "\n";
    out << std::string(width + 56, '-') << "\n";

    for (const auto& row : rows) {
        std::string name = row.name;
        if (name.size() > width) { name = name.substr(0, width - 3) + "..."; }
        out << std::left << std::setw(static_cast<int>(width)) << name
            << std::right << std::fixed << std::setprecision(4)
            << std::setw(8)  << row.calls
            << std::setw(12) << row.total_ms
            << std::setw(12) << row.average_ms()
            << std::setw(12) << row.min_ms
            << std::setw(12) << row.max_ms << "\n";
    }
    out << std::string(width + 56, '=') << "\n";
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
// The measuring side does NOT delegate to Profiler. Profiler writes five lines on
// every call; if Metrics went through it, that I/O would land inside the
// measurement window and a single call's "duration" would start with a few
// microseconds of cout cost. The only thing timed here is the call itself;
// reporting happens after the clock has stopped.
template <typename WrapperType>
struct Metrics {

    using CallableType = WrapperType;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType   = typename CallableType::ArgsType;
    using SourceLocation = std::source_location;
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
    inline static Duration total_duration{0};
    inline static Duration max_duration{0};
    inline static Duration min_duration{0};
    inline static StoredArgsType min_args{};
    inline static StoredArgsType max_args{};
    inline static SourceLocation last_call_location{};

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

        double average_ms() const {
            return calls ? total_duration.count() / calls : 0.0;
        }
    };

    static Snapshot snapshot() {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return Snapshot{call_count.load(std::memory_order_relaxed),
                        total_duration, min_duration, max_duration,
                        min_args,       max_args,     last_call_location,
                        has_samples};
    }

    static SourceLocation get_last_call_location() {
        const std::lock_guard<std::mutex> guard{stats_mutex};
        return last_call_location;
    }

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
        total_duration = Duration{0};
        max_duration = Duration{0};
        min_duration = Duration{0};
        min_args = StoredArgsType{};
        max_args = StoredArgsType{};
        last_call_location = SourceLocation{};
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
                                             state.has_samples};
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

    ReturnType operator()(auto&&... args) const
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return call_at(SourceLocation::current(), std::forward<decltype(args)>(args)...);
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
        Clock::time_point start = Clock::now();

        ~RecordOnExit() {
            // The clock stops before anything else, and before the LOCK in
            // particular: waiting on the lock is never added to the measurement.
            const Duration duration = Clock::now() - start;

            if (std::uncaught_exceptions() != exceptions_on_entry) {
                return; // the call threw, do not record a half-finished duration
            }

            // One critical section covering both the update and the reporting.
            // The report is inside the lock too, because otherwise the lines from
            // two threads would interleave. The user's function has already
            // returned, so the lock is not holding it.
            const std::lock_guard<std::mutex> guard{stats_mutex};

            last_call_location = location;
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
    return Metrics<Functor<std::decay_t<F>>>{wrap(std::forward<F>(target))};
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
