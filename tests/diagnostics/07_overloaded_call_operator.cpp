// EXPECT: not a callable object tempo can read
// Two operator() in one class: &F::operator() cannot pick one.
#include "tempo.hpp"

struct Overloaded {
    int operator()(int a) { return a; }
    int operator()(int a, int b) { return a + b; }
};

int main() {
    auto metrics = tempo::measure(Overloaded{});
    (void)metrics;
    return 0;
}
