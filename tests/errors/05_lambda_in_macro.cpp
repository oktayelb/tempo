// EXPECT: not a function or member function pointer
// Pointing the macros at a lambda instead of using tempo::measure.
#include "tempo.hpp"

auto add = [](int a, int b) { return a + b; };

int main() {
    tempo::CallableMetrics<&add> metrics;
    return metrics(1, 2);
}
