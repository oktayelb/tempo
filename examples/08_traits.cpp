// 08 — Reading a signature at compile time
//
// Underneath the profiling, tempo is a signature reader: hand it a function or
// method pointer and it hands back a type you can query. This is what makes the
// wrappers transparent, and it is usable on its own if you are building
// something else on top. All of it is free -- nothing below survives to runtime.

#include "tempo.hpp"

#include <iostream>
#include <string>
#include <tuple>
#include <type_traits>

double scale(const std::string& text, double factor) { return text.size() * factor; }

struct Session {
    int counter = 0;
    bool is_open() const { return counter > 0; }
};

int main() {
    using Scale = TEMPO_FUNCTION(scale);
    using IsOpen = TEMPO_METHOD(Session::is_open);

    // Parameters survive exactly as declared -- the reference is still there.
    static_assert(std::is_same_v<Scale::ReturnType, double>);
    static_assert(std::is_same_v<Scale::ArgsType, std::tuple<const std::string&, double>>);
    static_assert(Scale::arg_count == 2);
    static_assert(!Scale::is_member);

    // A method additionally carries its class and its constness. The instance is
    // not a parameter, so it does not appear in ArgsType.
    static_assert(std::is_same_v<IsOpen::ClassType, Session>);
    static_assert(std::is_same_v<IsOpen::ArgsType, std::tuple<>>);
    static_assert(IsOpen::is_member);
    static_assert(IsOpen::is_const_member);

    // The concepts are usable directly, which is how the macros reject a bad
    // argument at the call site instead of somewhere inside the library.
    static_assert(tempo::callable_traits::FunctionPointer<&scale>);
    static_assert(tempo::callable_traits::MethodPointer<&Session::is_open>);
    static_assert(!tempo::callable_traits::MethodPointer<&scale>);

    std::cout << "everything above was checked while compiling\n"
              << "scale : " << Scale::arg_count << " args, "
              << Scale::total_arg_size << " bytes of parameters\n";
}
