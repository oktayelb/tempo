#include <iostream>
#include <type_traits>
#include <chrono>
using namespace std;

namespace tempo{

// İlk tanım her zaman bu şekilde, structta <> yok
template <auto Func>
struct Function;

// İkinci tanım daha spesifik, ve struct adından sonra <> olmak zorunda.
template <typename ReturnType, typename... Args, ReturnType(*func_ptr)(Args...)>
struct Function<func_ptr> {
    

    using  return_type = ReturnType;
    using  args_type   = std::tuple<Args...>;
    ReturnType operator()(Args... args) const {
        return func_ptr(args...);
    }
};

template <auto Func>
struct FunctionTimer;

template <typename ReturnType, typename... Args, ReturnType(*func_ptr)(Args...)>
struct FunctionTimer<func_ptr> {

    using RetType = ReturnType;

    RetType operator()(Args... args) const {
        
        std::cout << "[Logger] Starting execution...\n";
        
        // --- START THE CLOCK ---
        auto start = std::chrono::high_resolution_clock::now();
        
        if constexpr (std::is_same_v<RetType, void>) {
            func_ptr(args...);
        
            auto end = std::chrono::high_resolution_clock::now();
            
            // Calculate duration in milliseconds (using double for decimals)
            std::chrono::duration<double, std::milli> duration = end - start;
            std::cout << "[Logger] Function Ran. Took: " << duration.count() << " ms\n";
        } 
        else {
            const RetType result = func_ptr(args...);
            
            // --- STOP THE CLOCK ---
            auto end = std::chrono::high_resolution_clock::now();
            
            std::chrono::duration<double, std::milli> duration = end - start;
            std::cout << "[Logger] Function Ran. Took: " << duration.count() << " ms\n";
            
            return result;
        }
    }
};
}









