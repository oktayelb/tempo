// 09 — Instrument once, leave every call site alone
//
// The other examples call through TEMPO_METRICS_CALL, which means editing every
// call site. TEMPO_INSTRUMENT is the alternative: name the function once at its
// declaration and the rest of the code keeps calling it normally.
//
// The trick is a name seam. A variable and a function cannot share a name in one
// scope, but they can across scopes -- so the real function lives in a nested
// namespace and the wrapper takes its name outside. Call sites resolve to the
// wrapper's operator() without knowing anything changed.

#include "tempo.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <thread>

namespace impl {

int sleep_for(int milliseconds, int request_id) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return request_id;
}

int describe(const std::string& label, int value) {
    return static_cast<int>(label.size()) + value;
}

}  // namespace impl

TEMPO_INSTRUMENT(impl::sleep_for, sleep_for);
TEMPO_INSTRUMENT(impl::describe, describe);

int main() {
    // Ordinary calls. No macro, no wrapper object, nothing to remember.
    sleep_for(12, 101);
    sleep_for(4, 102);
    // Only read under TEMPO_ENABLED, but it has to be recorded here, next to the
    // call it names.
    [[maybe_unused]] const int slow_line = __LINE__ + 1;
    sleep_for(20, 103);

    describe("hello", 3);

#if TEMPO_ENABLED
    const auto stats = decltype(sleep_for)::snapshot();

    std::cout << "calls   : " << stats.calls << "\n";
    std::cout << "slowest : " << stats.max_duration.count() << " ms"
              << "  with args (" << std::get<0>(stats.max_args)
              << ", " << std::get<1>(stats.max_args) << ")\n";

    assert(stats.calls == 3);
    assert(std::get<1>(stats.max_args) == 103);  // the 20 ms call
    assert(std::get<1>(stats.min_args) == 102);  // the 4 ms call

    // The call site is still recorded, even though the call site is a plain
    // sleep_for(...). operator() carries a source_location whose default
    // argument is evaluated at the caller -- see detail::FixedSignatureCall.
    std::cout << "last call site: " << stats.last_call_location.file_name()
              << ":" << stats.last_call_location.line() << "\n";

    std::cout << "the 20 ms call was written on line " << slow_line << "\n";

    // A reference parameter costs nothing extra on this path; only a by-value
    // parameter handed an lvalue pays one additional move.
    assert(decltype(describe)::snapshot().calls == 1);

    tempo::report();
#else
    std::cout << "built with TEMPO_ENABLED=0: the names above are plain "
                 "function pointers and tempo is entirely out of the build.\n";
#endif
}
