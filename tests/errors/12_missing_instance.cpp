// EXPECT: cannot be invoked with the arguments you passed
// A member function wrapper called without the object in front.
#include "tempo.hpp"

struct Service { int handle(int a) { return a; } };

int main() {
    tempo::CallableMetrics<&Service::handle> metrics;
    return metrics(1);
}
