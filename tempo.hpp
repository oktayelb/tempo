#include <iostream>
#include <type_traits>
#include <chrono>
#include <atomic>
using namespace std;

namespace tempo{
//-------------------------------------------------------------------

// İlk tanım her zaman bu şekilde, structta <> yok
template <auto Func>
struct Function;
// İkinci tanım daha spesifik, ve struct adından sonra <> olmak zorunda.
template <typename ret, typename... args, ret(*func_ptr)(args...)>
struct Function<func_ptr> {

    using  ReturnType = ret;
    using  ArgsType   = std::tuple<args...>;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
    ReturnType operator()(args... arg) const {
        return func_ptr(arg...);
    }
};
//-------------------------------------------------------------------
// C++20 sonrasında TMP daha temiz
template <auto func_ptr>
struct FunctionTimer {

    using FunctionType =  Function<func_ptr>;
    using ReturnType = typename FunctionType::ReturnType;
    using ArgsType   = typename FunctionType::ArgsType;
    
    inline static std::chrono::duration<double, std::milli> total_duration{0};
    inline static std::atomic<unsigned int> call_count{0};
    ReturnType operator()(auto... args) const {
        
        std::cout << "[Logger] Starting execution...\n";
        auto start = std::chrono::high_resolution_clock::now();
        call_count++;
        if constexpr (std::is_same_v<ReturnType, void>) {
            func_ptr(args...);
        
            auto end = std::chrono::high_resolution_clock::now();

            std::chrono::duration<double, std::milli> duration = end - start;
            total_duration += duration; 
            std::cout << "[Logger] Total size of args:" <<FunctionType::total_arg_size <<" bytes\n";
            std::cout << "[Logger] Function Ran. Took: " << duration.count() << " ms\n";
            std::cout << "[Logger] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[Logger] Call count: " << call_count << "\n";
            std::cout << "[Logger] Average time : " << (total_duration.count() / call_count) << " ms\n";
        } 
        else {
            const ReturnType result = func_ptr(args...);
            
            auto end = std::chrono::high_resolution_clock::now();
            
            std::chrono::duration<double, std::milli> duration = end - start;
            total_duration += duration;
            std::cout << "[Logger] Total size of args:" <<FunctionType::total_arg_size <<" bytes\n";
            std::cout << "[Logger] Function Ran. Took: " << duration.count() << " ms\n";
            std::cout << "[Logger] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[Logger] Call count: " << call_count << "\n";
            std::cout << "[Logger] Average time : " << (total_duration.count() / call_count) << " ms\n";
            return result;
            }
        }
    };
}
