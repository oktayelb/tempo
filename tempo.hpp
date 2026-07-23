#include <iostream>
#include <type_traits>
#include <chrono>
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
    ReturnType operator()(args... arg) const {
        return func_ptr(arg...);
    }
};
//-------------------------------------------------------------------
// C++20 sonrasında TMP daha temiz
template <auto func_ptr>
struct FunctionTimer {

    using ReturnType = typename Function<func_ptr>::ReturnType;
    using ArgsType   = typename Function<func_ptr>::ArgsType;
    inline static std::chrono::duration<double, std::milli> total_duration{0};
    inline static unsigned int call_count{0};
    ReturnType operator()(auto... args) const {
        
        std::cout << "[Logger] Starting execution...\n";
        auto start = std::chrono::high_resolution_clock::now();
        call_count++;
        if constexpr (std::is_same_v<ReturnType, void>) {
            func_ptr(args...);
        
            auto end = std::chrono::high_resolution_clock::now();

            std::chrono::duration<double, std::milli> duration = end - start;
            total_duration += duration; 
            std::cout << "[Logger] Function Ran. Took: " << duration.count() << " ms\n";
            std::cout << "[Logger] Total time spent on : " << total_duration.count() << " ms\n";
            std::cout << "[Logger] Call count: " << call_count << "\n";
        } 
        else {
            const ReturnType result = func_ptr(args...);
            
            auto end = std::chrono::high_resolution_clock::now();
            
            std::chrono::duration<double, std::milli> duration = end - start;
            total_duration += duration;
            std::cout << "[Logger] Function Ran. Took: " << duration.count() << " ms\n";
            std::cout << "[Logger] Total time spent on: " << total_duration.count() << " ms\n";
            std::cout << "[Logger] Call count: " << call_count << "\n";

            return result;
        }
    }
};




}








