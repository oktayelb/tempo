// EXPECT: this function's type is not supported
// This one does NOT reject itself: deduction against ret(*)(args...) succeeds
// with the "..." silently dropped, so the assert has to detect it deliberately.
#include "tempo.hpp"

namespace impl { int total(int first, ...) { return first; } }
TEMPO_INSTRUMENT(impl::total, total);

int main() { return total(1); }
