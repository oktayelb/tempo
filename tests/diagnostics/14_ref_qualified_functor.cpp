// EXPECT: operator() has a shape tempo cannot read
// The object IS readable (it has one operator()), but the signature behind it
// is ref-qualified, so MemberSignature is what rejects it.
#include "tempo.hpp"

struct RefQualified { int operator()(int a) & { return a; } };

int main() {
    auto metrics = tempo::measure(RefQualified{});
    (void)metrics;
    return 0;
}
