// EXPECT: not a callable object tempo can read
// A generic lambda has no signature until it is called.
#include "tempo.hpp"

int main() {
    auto metrics = tempo::measure([](auto x) { return x; });
    (void)metrics;
    return 0;
}
