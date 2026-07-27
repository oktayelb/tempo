#pragma once

#if defined(_MSC_VER)
#if !defined(_MSVC_LANG) || _MSVC_LANG < 202002L
#error "tempo requires C++20 support"
#endif
#elif __cplusplus < 202002L
#error "tempo requires C++20 support"
#endif

#include <iostream>
#include <type_traits>
#include <chrono>
#include <atomic>
#include <concepts>
#include <functional>
#include <tuple>
#include <source_location>
#include <utility>

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
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
    inline static std::atomic<unsigned int> call_count{0};

    ReturnType operator()(args... arg) const {
        call_count++;
        return std::invoke(func_ptr, arg...);
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
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) +  ... +  0);
    inline static std::atomic<unsigned int> call_count{0};

    ReturnType operator()(ClassName& instance, args... arg) const {
        call_count++;
        return std::invoke(method, instance, arg...);
    }
};

template <typename ClassName,typename ret, typename...args , ret(ClassName::*method)(args...) const>
struct Method<method>{

    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    using ClassType = ClassName;
    static constexpr bool is_member = true;
    static constexpr bool is_const_member = true;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) +  ... +  0);
    inline static std::atomic<unsigned int> call_count{0};

    ReturnType operator()(const ClassName& instance, args... arg) const {
        call_count++;
        return std::invoke(method, instance, arg...);
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


/*-----------------------------------------------------------------------------
template <typename Type, Type var>
struct Variable{};


template <typename Type , Type t_obj >
struct Variable <t_obj>{

    using TypeName = typename Type;


};




//-----------------------------------------------------------------------------*/




// C++20 sonrasında TMP daha temiz
template <auto CallableValue>
requires SupportedCallable<CallableValue>
struct CallableProfiler{

    using CallableType = Callable<CallableValue>;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType = typename CallableType::ArgsType;

    using SourceLocation = std::source_location;
    inline static SourceLocation last_call_location{};
    static SourceLocation get_last_call_location() { return last_call_location; }
    
    CallableType callable;


    ReturnType call_at(SourceLocation location, auto&&... args) const
        requires std::invocable<CallableType, decltype(args)...>
    {
        last_call_location = location;
        std::cout << "[CallableProfiler] Starting execution...\n";
        std::cout << "[CallableProfiler] Call location: " << location.file_name() << ":" << location.line() << "\n";
        std::cout << "[CallableProfiler] Caller function: " << location.function_name() << "\n";
        std::cout << "[CallableProfiler] Total size of args:" << CallableType::total_arg_size << " bytes\n";

        if constexpr (std::is_same_v<ReturnType, void>) {
            callable(std::forward<decltype(args)>(args)...);
            std::cout << "[CallableProfiler] Call count: " << CallableType::call_count << "\n";
        }
        else {
            ReturnType result = callable(std::forward<decltype(args)>(args)...);
            std::cout << "[CallableProfiler] Call count: " << CallableType::call_count << "\n";
            return result;
        }

    }

    ReturnType operator()(auto&&... args) const
        requires std::invocable<CallableType, decltype(args)...>
    {
        return call_at(SourceLocation::current(), std::forward<decltype(args)>(args)...);
    }
};
//-------------------------------------------------------------------
template <auto CallableValue>
requires SupportedCallable<CallableValue>
struct CallableMetrics {

    using ProfilerType = CallableProfiler<CallableValue>;
    using CallableType = Callable<CallableValue>;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType   = typename CallableType::ArgsType;
    using SourceLocation = std::source_location;
    
    inline static std::chrono::duration<double, std::milli> total_duration{0};
    inline static std::chrono::duration<double, std::milli> max_duration{0};
    inline static std::chrono::duration<double, std::milli> min_duration{0};
    inline static ArgsType min_args{};
    inline static ArgsType max_args{};
    inline static auto& call_count = CallableType::call_count;
    inline static SourceLocation last_call_location{};

    static SourceLocation get_last_call_location() { return last_call_location; }

    static ArgsType make_args_tuple(auto&&... args) {
        if constexpr (std::is_member_function_pointer_v<decltype(CallableValue)>) {
            return make_args_tuple_without_instance(std::forward<decltype(args)>(args)...);
        }
        else {
            return ArgsType{std::forward<decltype(args)>(args)...};
        }
    }

    static void reset() {
        call_count.store(0);
        total_duration = std::chrono::duration<double, std::milli>{0};
        max_duration = std::chrono::duration<double, std::milli>{0};
        min_duration = std::chrono::duration<double, std::milli>{0};
        min_args = ArgsType{};
        max_args = ArgsType{};
        last_call_location = SourceLocation{};
    }

    ReturnType call_at(SourceLocation location, auto&&... args) const
        requires std::invocable<CallableType, decltype(args)...>
    {
        last_call_location = location;
        ProfilerType profiler;
        auto start = std::chrono::high_resolution_clock::now();
        if constexpr (std::is_same_v<ReturnType, void>) {
            
            profiler.call_at(location, std::forward<decltype(args)>(args)...);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration = end - start;

            total_duration += duration;
            if (duration > max_duration){
                max_duration = duration; 
                max_args = make_args_tuple(std::forward<decltype(args)>(args)...);
            }
            if (CallableType::call_count == 1 || duration < min_duration){
                min_duration = duration;
                min_args = make_args_tuple(std::forward<decltype(args)>(args)...);
            }
            std::cout << "[CallableMetrics] Callable ran. Took: " << duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Call location: " << location.file_name() << ":" << location.line() << "\n";
            std::cout << "[CallableMetrics] Caller function: " << location.function_name() << "\n";
            std::cout << "[CallableMetrics] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Min time : " << min_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Max time : " << max_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Average time : " << (total_duration.count() / CallableType::call_count) << " ms\n";
        } 
        else {
            
            const ReturnType result = profiler.call_at(location, std::forward<decltype(args)>(args)...);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration = end - start;
            
            total_duration += duration;
            if (duration > max_duration){
                max_duration = duration; 
                max_args = make_args_tuple(std::forward<decltype(args)>(args)...);
            }
            if (CallableType::call_count == 1 || duration < min_duration){
                min_duration = duration;
                min_args = make_args_tuple(std::forward<decltype(args)>(args)...);
            }
            std::cout << "[CallableMetrics] Callable ran. Took: " << duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Call location: " << location.file_name() << ":" << location.line() << "\n";
            std::cout << "[CallableMetrics] Caller function: " << location.function_name() << "\n";
            std::cout << "[CallableMetrics] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Min time : " << min_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Max time : " << max_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Average time : " << (total_duration.count() / CallableType::call_count) << " ms\n";
            return result;
            }
        }

    ReturnType operator()(auto&&... args) const
        requires std::invocable<CallableType, decltype(args)...>
    {
        return call_at(SourceLocation::current(), std::forward<decltype(args)>(args)...);
    }
private:
    template <typename Instance, typename... MethodArgs>
    static ArgsType make_args_tuple_without_instance(Instance&&, MethodArgs&&... arg) {
        return ArgsType{std::forward<MethodArgs>(arg)...};
    }

public:
    ArgsType get_minimizers() const {return min_args;}
    ArgsType get_maximizers() const {return max_args;}

    };

template <typename ClassType>
concept IsClass = std::is_class_v<ClassType>;

template <typename ClassType>
requires IsClass<ClassType>
struct ConstructorProfiler{

    inline static  unsigned int obj_count{0};
    template <typename... Args>
    ClassType operator() (Args... args) {
        ClassType obj(args...);
        obj_count++;
        return obj;

        };
    };
 }
