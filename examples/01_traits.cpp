// 01 — Compile-time introspection
//
// tempo's foundation: hand it a function or method pointer and it hands back a
// type whose signature you can query. Almost everything below is a
// static_assert, so this example proves its point while compiling and costs
// exactly nothing at runtime.

#include "tempo.hpp"

#include <iostream>
#include <string>
#include <tuple>
#include <type_traits>

int add(int a, int b) { return a + b; }
double scale(const std::string& text, double factor) { return text.size() * factor; }
void log_line(std::string message) { (void)message; }

struct Session {
    int counter = 0;
    int touch(int delta) { return counter += delta; }
    bool is_open() const { return counter > 0; }
};

int main() {
    // --- which pointers does tempo accept? -----------------------------------
    static_assert(tempo::FunctionPointer<&add>);
    static_assert(!tempo::FunctionPointer<&Session::touch>);
    static_assert(tempo::MethodPointer<&Session::touch>);
    static_assert(tempo::MethodPointer<&Session::is_open>);
    static_assert(!tempo::MethodPointer<&add>);
    static_assert(tempo::CallablePointer<&add>);
    static_assert(tempo::CallablePointer<&Session::is_open>);

    // --- a free function ------------------------------------------------------
    using Add = TEMPO_FUNCTION(add);
    static_assert(std::is_same_v<Add::ReturnType, int>);
    static_assert(std::is_same_v<Add::ArgsType, std::tuple<int, int>>);
    static_assert(std::is_same_v<Add::ClassType, void>);
    static_assert(Add::arg_count == 2);
    static_assert(!Add::is_member);
    static_assert(!Add::is_const_member);

    // --- reference parameters are preserved exactly in ArgsType ---------------
    using Scale = TEMPO_FUNCTION(scale);
    static_assert(std::is_same_v<Scale::ReturnType, double>);
    static_assert(std::is_same_v<Scale::ArgsType, std::tuple<const std::string&, double>>);

    // --- void returns are fine ------------------------------------------------
    using Log = TEMPO_FUNCTION(log_line);
    static_assert(std::is_same_v<Log::ReturnType, void>);
    static_assert(Log::arg_count == 1);

    // --- methods additionally carry their class and their constness ----------
    using Touch  = TEMPO_METHOD(Session::touch);
    using IsOpen = TEMPO_METHOD(Session::is_open);
    static_assert(std::is_same_v<Touch::ClassType, Session>);
    static_assert(std::is_same_v<Touch::ArgsType, std::tuple<int>>);
    static_assert(Touch::is_member);
    static_assert(!Touch::is_const_member);
    static_assert(IsOpen::is_member);
    static_assert(IsOpen::is_const_member);
    static_assert(IsOpen::arg_count == 0);

    // ArgsType holds only the declared parameters; the instance is not one.
    static_assert(std::tuple_size_v<Touch::ArgsType> == 1);

    std::cout << "signature                              args  member  const\n"
              << "---------------------------------------------------------\n";
    std::cout << "int add(int, int)                      "
              << Add::arg_count << "     " << Add::is_member << "       " << Add::is_const_member << "\n";
    std::cout << "double scale(const string&, double)    "
              << Scale::arg_count << "     " << Scale::is_member << "       " << Scale::is_const_member << "\n";
    std::cout << "void log_line(string)                  "
              << Log::arg_count << "     " << Log::is_member << "       " << Log::is_const_member << "\n";
    std::cout << "int Session::touch(int)                "
              << Touch::arg_count << "     " << Touch::is_member << "       " << Touch::is_const_member << "\n";
    std::cout << "bool Session::is_open() const          "
              << IsOpen::arg_count << "     " << IsOpen::is_member << "       " << IsOpen::is_const_member << "\n";

    // total_arg_size is the sum of sizeof over the *declared* parameter types.
    // It is a static number: a std::string parameter reports sizeof(std::string)
    // no matter how large the string's heap buffer actually is.
    std::cout << "\ntotal_arg_size(add)      = " << Add::total_arg_size << " bytes\n";
    std::cout << "total_arg_size(log_line) = " << Log::total_arg_size
              << " bytes  (sizeof(std::string), not the text length)\n";

    std::cout << "\nEverything above except the printing was checked at compile time.\n";
}
