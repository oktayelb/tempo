// EXPECT: this function's type is not supported
// A noexcept free function: the commonest way into the old wall of errors.
#include "tempo.hpp"

namespace impl { int scale(int a) noexcept { return a * 2; } }
TEMPO_INSTRUMENT(impl::scale, scale);

int main() { return scale(1); }
