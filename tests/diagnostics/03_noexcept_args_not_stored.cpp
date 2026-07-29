// EXPECT: get_maximizers() is unavailable
// The price of wrapping a noexcept callable. std::string is copyable and default
// constructible, so a throwing function taking one captures its arguments
// happily -- but copying it allocates, and tempo will not let instrumentation
// throw out of a wrapper that has promised not to. Capture switches itself off
// instead, and asking for the maximizers has to say so rather than returning an
// empty tuple.
#include "tempo.hpp"
#include <string>

namespace impl { std::size_t width(std::string text) noexcept { return text.size(); } }
TEMPO_INSTRUMENT(impl::width, width);

int main() {
    auto worst = width.get_maximizers();
    (void)worst;
    return 0;
}
