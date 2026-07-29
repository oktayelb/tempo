// EXPECT: this function's type is not supported
// A noexcept function is supported now. A noexcept C-STYLE VARIADIC one is not,
// and it has to be rejected for the ellipsis rather than for the noexcept -- the
// message names the '...' and offers the lambda, exactly as the plain variadic
// case in 02 does.
#include "tempo.hpp"

namespace impl { int total(int first, ...) noexcept { return first; } }
TEMPO_INSTRUMENT(impl::total, total);

int main() { return total(1); }
