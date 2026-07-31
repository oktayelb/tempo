// EXPECT: the template argument must be a class or struct
#include "tempo.hpp"

int main() {
    tempo::ConstructorProfiler<int> make;
    return make(5);
}
