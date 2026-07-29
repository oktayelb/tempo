// EXPECT: not a tempo wrapper type
// tempo::Metrics wraps a wrapper, not a raw lambda type. This should have been
// tempo::measure(add).
#include "tempo.hpp"

int main() {
    auto add = [](int a) { return a; };
    tempo::Metrics<decltype(add)> metrics;
    (void)metrics;
    return 0;
}
