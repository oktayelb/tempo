// EXPECT: this member function's type is not supported
// A ref-qualified member function: ret(Class::*)(args...) & matches nothing.
#include "tempo.hpp"

struct Service { int handle(int a) & { return a; } };

int main() {
    tempo::CallableMetrics<&Service::handle> metrics;
    Service service;
    return metrics(service, 1);
}
