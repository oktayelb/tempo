// EXPECT: this member function's type is not supported
#include "tempo.hpp"

struct Service { int handle(int a) noexcept { return a; } };

int main() {
    tempo::CallableMetrics<&Service::handle> metrics;
    Service service;
    return metrics(service, 1);
}
