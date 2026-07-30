// EXPECT: slowest_args() is unavailable
// A move-only parameter switches argument capture off, so there are no recorded
// arguments to ask for. Without the assert this surfaces inside <tuple>.
#include "tempo.hpp"
#include <memory>

namespace impl { int consume(std::unique_ptr<int> owned) { return *owned; } }
TEMPO_INSTRUMENT(impl::consume, consume);

int main() {
    auto worst = consume.slowest_args();
    (void)worst;
    return 0;
}
