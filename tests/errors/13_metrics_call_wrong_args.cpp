// EXPECT: cannot be invoked with the arguments you passed
#include "tempo.hpp"
#include <string>

struct Service { int handle(int a) { return a; } };

int main() {
    tempo::CallableMetrics<&Service::handle> metrics;
    Service service;
    return metrics(service, std::string("not an int"));
}
