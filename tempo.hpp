#pragma once

// tempo — Copyright (c) 2026 Oktay Elibüyük
// Released under the MIT License. See LICENSE for the full terms.

// See the README: Version.
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

// See the README: Reporting.
#ifndef TEMPO_PRINT_ENABLED
#define TEMPO_PRINT_ENABLED 0
#endif

// See the README: Instrumenting without touching call sites -- for this and for
// TEMPO_INSTRUMENT below it.
#ifndef TEMPO_ENABLED
#define TEMPO_ENABLED 1
#endif

#if TEMPO_ENABLED
#define TEMPO_INSTRUMENT(function, alias) inline ::tempo::CallableMetrics<&function> alias{}
#else
#define TEMPO_INSTRUMENT(function, alias) inline constexpr auto alias = &function
#endif

// See the README: Recursion -- for this and for everything below it, down to
// TEMPO_SELF.
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
#define TEMPO_CALLABLE_METRICS(callable) ::tempo::CallableMetrics<&callable>
#define TEMPO_PROFILE_CALL(profiler, ...) (profiler).call_at(::std::source_location::current() __VA_OPT__(,) __VA_ARGS__)
#define TEMPO_METRICS_CALL(metrics, ...) (metrics).call_at(::std::source_location::current() __VA_OPT__(,) __VA_ARGS__)

namespace tempo{

using CallCount =  std::uint64_t;

//Value type name değil, fonksiyonlar ve metodların ta kendisi, 
//bunları Fonksiyon classının valueları gibi düşünebiliriz
template <auto Value>
concept FunctionPointer =
    std::is_pointer_v<decltype(Value)> &&
    std::is_function_v<std::remove_pointer_t<decltype(Value)>>;

template <auto Value>
concept MethodPointer = std::is_member_function_pointer_v<decltype(Value)>;

template <auto Value>
concept CallablePointer = FunctionPointer<Value> || MethodPointer<Value>;


namespace detail::storage {


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

} // namespace detail::storage


namespace detail {
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


struct UnsupportedSignature {
    using ReturnType = UnsupportedReturn;
    using ArgsType   = std::tuple<UnsupportedArg>;
    static constexpr bool is_const = true;
    static constexpr bool is_noexcept = false;
    static constexpr std::size_t total_arg_size = 0;
};

} // namespace detail

namespace detail::reporting {


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


struct ReportRow {
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

using RowFetcher = ReportRow (*)();
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

inline void add_to_registry(RowFetcher fetcher, Resetter resetter) {
    Registry& reg = registry();
    const std::lock_guard<std::mutex> guard{reg.mutex};
    reg.fetchers.push_back(fetcher);
    reg.resetters.push_back(resetter);
}

} // namespace detail::reporting

// A summary of every registered metric: one table, sorted by total time.
inline void report(std::ostream& out = std::cout) {
    std::vector<detail::reporting::ReportRow> rows;
    {
        detail::reporting::Registry& reg = detail::reporting::registry();
        const std::lock_guard<std::mutex> guard{reg.mutex};
        rows.reserve(reg.fetchers.size());
        for (const detail::reporting::RowFetcher fetch : reg.fetchers) { rows.push_back(fetch()); }
    }

    std::erase_if(rows, [](const detail::reporting::ReportRow& row) { return row.calls == 0; });
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
        rows, [](const detail::reporting::ReportRow& row) { return row.max_depth > 1; });
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
    detail::reporting::Registry& reg = detail::reporting::registry();
    const std::lock_guard<std::mutex> guard{reg.mutex};
    for (const detail::reporting::Resetter reset : reg.resetters) { reset(); }
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


template <typename F>
concept CallableObject =
    std::is_class_v<F> &&
    requires { &F::operator(); };

namespace detail::signature {

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


template <typename F>
struct FunctorSignature {
    using Type = UnsupportedSignature;
};

template <typename F>
requires CallableObject<F>
struct FunctorSignature<F> {
    using Type = MemberSignature<decltype(&F::operator())>;
};

} // namespace detail::signature

// Is a type one of tempo's own wrapper types, and what to fall back on when it
// is not. This is the guard on the front door of Metrics and Profiler.
namespace detail::wrapper {


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

} // namespace detail::wrapper

namespace detail {


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

    static_assert(CallablePointer<CallableValue>,
        "tempo: the template argument is not a function or member function pointer.\n"
        "  TEMPO_INSTRUMENT, TEMPO_CALLABLE_METRICS and TEMPO_CALLABLE_PROFILER take\n"
        "  the ADDRESS OF A FUNCTION:\n"
        "      TEMPO_INSTRUMENT(impl::my_function, my_function);\n"
        "  If you passed a lambda, a functor or a std::function, use a factory\n"
        "  instead -- those are objects, not function pointers, and an object\n"
        "  cannot be a template argument:\n"
        "      auto m = tempo::measure(my_lambda);\n"
        "      TEMPO_METRICS_CALL(m, arg1, arg2);");


    using CallableType = typename CallableImplementation<CallableValue>::Type;
    using CallableType::operator();
};


template <typename F>
struct Functor {

    static_assert(CallableObject<F>,
        "tempo: this is not a callable object tempo can read.\n"
        TEMPO_NOT_A_CALLABLE_OBJECT_MESSAGE);

    using SignatureType = typename detail::signature::FunctorSignature<F>::Type;
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



namespace detail::wrapper {


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

} // namespace detail::wrapper

// TMP is cleaner since C++20.
//
// Templated on the wrapper type (Callable<&f> or Functor<Lambda>), not on an
// NTTP: a lambda cannot be a template argument, but its type can.
template <typename WrapperType>
struct Profiler{

    static_assert(detail::wrapper::TempoWrapper<WrapperType> ||
                  detail::wrapper::is_tempo_wrapper_template<WrapperType>,
        "tempo::Profiler: this is not a tempo wrapper type.\n"
        TEMPO_NOT_A_WRAPPER_MESSAGE);


    using CallableType = typename detail::wrapper::WrapperOrStandIn<WrapperType>::Type;
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


    // Whether the wrapped callable promised not to throw, and therefore whether
    // this wrapper does too.
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


template <auto CallableValue>
using CallableProfiler = Profiler<Callable<CallableValue>>;
//-------------------------------------------------------------------
// Which operator() Metrics puts on its front.
namespace detail::call {


template <typename Derived, typename CallableType>
struct VariadicCall {
    typename CallableType::ReturnType operator()(auto&&... args) const
        noexcept(CallableType::is_noexcept)
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return static_cast<const Derived&>(*this).call_at(
            std::source_location::current(), std::forward<decltype(args)>(args)...);
    }


    typename CallableType::ReturnType operator()(auto&&... args) const
        requires (!std::invocable<const CallableType&, decltype(args)...>)
    {
        static_assert(always_false<decltype(args)...>,
            "tempo: this callable cannot be invoked with the arguments you passed.\n"
            TEMPO_BAD_CALL_ARGUMENTS_MESSAGE);
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
};

template <typename Derived, typename CallableType>
using CallOperator = std::conditional_t<
    CallableType::is_member,
    VariadicCall<Derived, CallableType>,
    FixedSignatureCall<Derived, CallableType, typename CallableType::ArgsType>>;

} // namespace detail::call


template <typename WrapperType>
struct Metrics : detail::call::CallOperator<Metrics<WrapperType>,
                                      typename detail::wrapper::WrapperOrStandIn<WrapperType>::Type> {

    static_assert(detail::wrapper::TempoWrapper<WrapperType> ||
                  detail::wrapper::is_tempo_wrapper_template<WrapperType>,
        "tempo::Metrics: this is not a tempo wrapper type.\n"
        TEMPO_NOT_A_WRAPPER_MESSAGE);


    using CallableType = typename detail::wrapper::WrapperOrStandIn<WrapperType>::Type;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType   = typename CallableType::ArgsType;
    using SourceLocation = std::source_location;

    // Metrics declares no operator() of its own -- it inherits exactly one, so
    // there is never an overload to resolve between. See detail::call::CallOperator.
    using CallOperatorBase = detail::call::CallOperator<Metrics<WrapperType>, CallableType>;
    using CallOperatorBase::operator();

    using Clock = std::chrono::steady_clock;
    static_assert(Clock::is_steady, "tempo requires a monotonic clock to measure durations");
    using Duration = std::chrono::duration<double, std::milli>;

    // Whether the wrapped callable promised not to throw, and therefore whether
    // this wrapper does too.
    static constexpr bool is_noexcept = CallableType::is_noexcept;


    static constexpr bool tracks_args =
        detail::storage::ArgsAreStorable<ArgsType>::value &&
        (!is_noexcept || detail::storage::ArgsAreNothrowStorable<ArgsType>::value);

    // The signature with its references stripped: storable and assignable.
    using StoredArgsType = std::conditional_t<
        tracks_args,
        typename detail::storage::DecayedTuple<ArgsType>::Type,
        std::tuple<>>;

    // call_count is atomic and lives on the wrapper, so it can be read directly.
    inline static auto& call_count = CallableType::call_count;

private:

    inline static std::mutex stats_mutex;
    inline static bool has_samples = false;


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

    }


    static void ensure_registered() {
        static const bool once = [] {
            detail::reporting::add_to_registry(
                [] {
                    const Snapshot state = snapshot();
                    return detail::reporting::ReportRow{std::string{detail::reporting::type_name<CallableType>()},
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
        static_assert(detail::always_false<decltype(args)...>,
            "tempo: TEMPO_METRICS_CALL -- this callable cannot be invoked with these arguments.\n"
            TEMPO_BAD_CALL_ARGUMENTS_MESSAGE);
    }


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


template <auto CallableValue>
using CallableMetrics = Metrics<Callable<CallableValue>>;


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
