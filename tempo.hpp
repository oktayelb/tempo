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

template <auto func_ptr>
struct FunctionTimer {



    using ReturnType = typename Function<func_ptr>::ReturnType;
    using ArgsType   = typename Function<func_ptr>::ArgsType;

    ReturnType operator()(typename... args) const {
        
        std::cout << "[Logger] Starting execution...\n";
        auto start = std::chrono::high_resolution_clock::now();
        
        if constexpr (std::is_same_v<ReturnType, void>) {
            func_ptr(args...);
        
            auto end = std::chrono::high_resolution_clock::now();
            
            std::chrono::duration<double, std::milli> duration = end - start;
            std::cout << "[Logger] Function Ran. Took: " << duration.count() << " ms\n";
        } 
        else {
            const ReturnType result = func_ptr(args...);
            
            auto end = std::chrono::high_resolution_clock::now();
            
            std::chrono::duration<double, std::milli> duration = end - start;
            std::cout << "[Logger] Function Ran. Took: " << duration.count() << " ms\n";
            
            return result;
        }
    }
};




}









