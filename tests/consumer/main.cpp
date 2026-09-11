#include <tempo.hpp>

int twice(int value) { return value * 2; }

int main() {
    tempo::CallableMetrics<&twice> metric;
    return metric(21) == 42 ? 0 : 1;
}
