#pragma once

#include <iostream>
#include <type_traits>
#include <chrono>
#include <atomic>
#include <tuple>

namespace tempo{
//-------------------------------------------------------------------
// İlk tanım her zaman bu şekilde, structta <> yok
template<auto Func>
struct Function;
// İkinci tanımda da <> olmak zorunda
template <typename ret, typename... args, ret(*func_ptr)(args...)>
struct Function<func_ptr>{

    using  ReturnType = ret;
    using  ArgsType   = std::tuple<args...>;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
    inline static std::atomic<unsigned int> call_count{0};

    ReturnType operator()(args... arg) const {
        call_count++;
        return func_ptr(arg...);
    }

};




//-----------------------------------------------------------------------------


// C++20 sonrasında TMP daha temiz
template <auto func_ptr>
struct FunctionProfiler{

    using FunctionType = Function<func_ptr>;
    using ReturnType = typename FunctionType::ReturnType;
    using ArgsType = typename FunctionType::ArgsType;

    FunctionType function;

    ReturnType operator()(auto... args) const {
        std::cout << "[FunctionProfiler] Starting execution...\n";
        std::cout << "[FunctionProfiler] Total size of args:" << FunctionType::total_arg_size << " bytes\n";

        if constexpr (std::is_same_v<ReturnType, void>) {
            function(args...);
            std::cout << "[FunctionProfiler] Call count: " << FunctionType::call_count << "\n";
        }
        else {
            ReturnType result = function(args...);
            std::cout << "[FunctionProfiler] Call count: " << FunctionType::call_count << "\n";
            return result;
        }

    }
};
//-------------------------------------------------------------------
template <auto func_ptr>
struct FunctionMetrics {

    using ProfilerType = FunctionProfiler<func_ptr>;
    using FunctionType = Function<func_ptr>;
    using ReturnType = typename ProfilerType::ReturnType;
    using ArgsType   = typename ProfilerType::ArgsType;
    
    inline static std::chrono::duration<double, std::milli> total_duration{0};
    inline static std::chrono::duration<double, std::milli> max_duration{0};
    inline static std::chrono::duration<double, std::milli> min_duration{0};
    inline static ArgsType min_args{};
    inline static ArgsType max_args{};
    inline static auto& call_count = FunctionType::call_count;

    static void reset() {
        call_count.store(0);
        total_duration = std::chrono::duration<double, std::milli>{0};
        max_duration = std::chrono::duration<double, std::milli>{0};
        min_duration = std::chrono::duration<double, std::milli>{0};
        min_args = ArgsType{};
        max_args = ArgsType{};
    }

    ReturnType operator()(auto... args) const {
        ProfilerType function;
        auto start = std::chrono::high_resolution_clock::now();
        if constexpr (std::is_same_v<ReturnType, void>) {
            
            function(args...);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration = end - start;

            total_duration += duration;
            if (duration > max_duration){
                max_duration = duration; 
                max_args = ArgsType{args...};
            }
            if (FunctionType::call_count == 1 || duration < min_duration){
                min_duration = duration;
                min_args = ArgsType{args...};
            }
            std::cout << "[FunctionMetrics] Function Ran. Took: " << duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Min time : " << min_duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Max time : " << max_duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Average time : " << (total_duration.count() / FunctionType::call_count) << " ms\n";
        } 
        else {
            
            const ReturnType result = function(args...);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration = end - start;
            
            total_duration += duration;
            if (duration > max_duration){
                max_duration = duration; 
                max_args = ArgsType{args...};
            }
            if (FunctionType::call_count == 1 || duration < min_duration){
                min_duration = duration;
                min_args = ArgsType{args...};
            }
            std::cout << "[FunctionMetrics] Function Ran. Took: " << duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Min time : " << min_duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Max time : " << max_duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Average time : " << (total_duration.count() / FunctionType::call_count) << " ms\n";
            return result;
            }
        }
    
    ArgsType get_minimizers() const {return min_args;}
    ArgsType get_maximizers() const {return max_args;}

    };

template <typename ClassType>
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
