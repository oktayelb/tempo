// EXPECT: TEMPO_METRICS_CALL
#include "tempo.hpp"
#include <string>

struct Service { int handle(int a) { return a; } };

int main() {
    tempo::CallableMetrics<&Service::handle> metrics;
    Service service;
    return TEMPO_METRICS_CALL(metrics, service, std::string("not an int"));
}
