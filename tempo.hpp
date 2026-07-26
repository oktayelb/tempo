#include <iostream>
#include <type_traits>
#include <chrono>
#include <atomic>
#include <tuple>
using namespace std;

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
        std::cout << "[FunctionProfiler] Call count: " << FunctionType::call_count << "\n";

        return function(args...);

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
    ReturnType operator()(auto... args) const {
        
        auto start = std::chrono::high_resolution_clock::now();
        if constexpr (std::is_same_v<ReturnType, void>) {
            
            ProfilerType var;
            var(args...);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration = end - start;
            total_duration += duration; 
            std::cout << "[FunctionMetrics] Function Ran. Took: " << duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Average time : " << (total_duration.count() / FunctionType::call_count) << " ms\n";
        } 
        else {
            ProfilerType var;
            const ReturnType result = var(args...);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration = end - start;
            total_duration += duration;
            std::cout << "[FunctionMetrics] Function Ran. Took: " << duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[FunctionMetrics] Average time : " << (total_duration.count() / FunctionType::call_count) << " ms\n";
            return result;
            }
        }
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
