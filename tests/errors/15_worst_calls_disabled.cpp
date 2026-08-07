// EXPECT: worst_calls() is unavailable
// A capacity of 0 stores no ranking, so there is nothing for worst_calls() to
// return. Without the assert this surfaces as an out-of-range std::array.
#include "tempo.hpp"

namespace impl { int render(int width) { return width; } }
TEMPO_INSTRUMENT(impl::render, render, 0);

int main() {
    const auto ranked = render.worst_calls();
    (void)ranked;
    return 0;
}
