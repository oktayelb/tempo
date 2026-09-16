// Timing helpers for the tests that have to make one call take longer than
// another, so that a ranking, a fastest or a slowest has something to find.
//
// Both tools here exist because of the same mistake, which is worth stating
// plainly: asking for a 2 ms call and a 25 ms call and then asserting that the
// 25 ms one came out slowest is an assertion about the SCHEDULER, not about
// tempo. It holds on an idle laptop and stops holding on a loaded CI runner,
// where it produced failures on the macOS lane that said nothing about the
// library.
//
//   busy_ms   sets a reliable floor. A sleeping call has to be woken again, and
//             wake-up latency on a busy machine is unbounded; spinning never
//             asks to be rescheduled.
//
//   Observer  removes the assumption entirely. It times each call from the
//             caller's side and lets the expectation be DERIVED from what
//             actually happened. A stall inside a call inflates tempo's
//             measurement and the observer's alike, so the two can only
//             disagree over the sliver of wrapper bookkeeping outside tempo's
//             own timer -- microseconds, against gaps of milliseconds.
//
// The point is not to make the tests lenient. Every assertion is still exact;
// it is the comparison that is now against reality rather than against a
// request the operating system never promised to honour.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

namespace tempo_test {

// Occupies the CPU for a given number of milliseconds and returns its tag, so
// it can stand in for any "work that takes a while" callable.
//
// steady_clock::now() is an opaque call, so the loop cannot be optimised away.
inline int busy_ms(int milliseconds, int tag) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
    }
    return tag;
}

// Remembers what the caller observed for each call it made.
struct Observer {
    using Duration = std::chrono::duration<double, std::milli>;

    struct Call {
        int milliseconds;   // what was asked for
        int tag;            // what identifies it in the stored arguments
        Duration duration;  // what it actually took
    };

    std::vector<Call> calls;

    // The general form, for when the call cannot be spelled as
    // metric(milliseconds, tag) -- a member function takes its instance first.
    template <typename Invoke>
    void call(int milliseconds, int tag, Invoke&& invoke) {
        const auto start = std::chrono::steady_clock::now();
        invoke();
        calls.push_back({milliseconds, tag, std::chrono::steady_clock::now() - start});
    }

    // The common form: a metric over a busy_ms-shaped (milliseconds, tag).
    template <typename Metrics>
    void call(Metrics& metrics, int milliseconds, int tag) {
        call(milliseconds, tag, [&] { metrics(milliseconds, tag); });
    }

    // Every call, slowest first. stable_sort matches how tempo breaks a tie:
    // rank_worst inserts on a strict >, so a call that ties an existing entry
    // lands after it, in call order.
    std::vector<Call> by_duration() const {
        std::vector<Call> ordered = calls;
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const Call& left, const Call& right) {
                             return left.duration > right.duration;
                         });
        return ordered;
    }

    // The tags of the `count` slowest calls, slowest first.
    std::vector<int> slowest_tags(std::size_t count) const {
        std::vector<Call> ordered = by_duration();
        ordered.resize(std::min(count, ordered.size()));

        std::vector<int> tags;
        for (const Call& entry : ordered) { tags.push_back(entry.tag); }
        return tags;
    }

    Call slowest() const { return by_duration().front(); }
    Call fastest() const { return by_duration().back(); }
};

}  // namespace tempo_test
