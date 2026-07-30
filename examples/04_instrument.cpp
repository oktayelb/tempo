// 04 — Instrument once, leave every call site alone
//
// Everything so far needed a wrapper object at the call site. TEMPO_INSTRUMENT
// is the alternative: name the function once at its declaration and the rest of
// the program keeps calling it normally. The real function lives in a nested
// namespace and the wrapper takes over its name, so call sites resolve to the
// wrapper without knowing anything changed.
//
// Build with -DTEMPO_ENABLED=0 and the name is a plain function pointer again,
// with tempo entirely out of the binary.

#include "tempo.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace impl {

int handle_request(int milliseconds, int request_id) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return request_id;
}

}  // namespace impl

TEMPO_INSTRUMENT(impl::handle_request, handle_request);

int main() {
    // Ordinary calls. No macro, no wrapper object, nothing to remember.
    handle_request(12, 101);
    handle_request(4, 102);
    handle_request(20, 103);

#if TEMPO_ENABLED
    const auto stats = handle_request.snapshot();
    const auto [slowest_ms, slowest_id] = stats.max_args;

    std::cout << "calls   : " << stats.calls << "\n"
              << "slowest : request " << slowest_id
              << "  (" << stats.max_duration.count() << " ms, asked for "
              << slowest_ms << ")\n";

    // The call site is still captured, even though the call is a plain
    // handle_request(...): operator() carries a source_location whose default
    // argument is evaluated at the caller.
    std::cout << "last call site: " << stats.last_call_location.file_name()
              << ":" << stats.last_call_location.line() << "\n";
#else
    std::cout << "built with TEMPO_ENABLED=0: handle_request is a plain "
                 "function pointer and tempo is out of the build.\n";
#endif
}
