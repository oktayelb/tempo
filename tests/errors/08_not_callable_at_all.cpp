// EXPECT: not a callable object tempo can read
#include "tempo.hpp"

int main() {
    auto metrics = tempo::measure(42);
    (void)metrics;
    return 0;
}
