// A tiny assertion harness for tempo's tests.
//
// No external dependency on purpose: tempo is a single header, and its test
// suite should not need more infrastructure than tempo does. Each test file is
// its own binary, exactly like the examples, and exits non-zero if any check
// fails so a CI job can just run it.
//
//     TEST(name) { CHECK_EQ(a, b); }
//
// Timing checks deserve a warning. CI machines are noisy and shared, so nothing
// here asserts an absolute duration. Tests assert structural invariants instead
// -- min <= average <= max, total no larger than the wall clock, a counter that
// returned to zero -- which are true regardless of how slow the machine is.

#pragma once

#include <cmath>
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace tempo_test {

struct TestCase {
    const char* name;
    void (*run)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

inline int checks_run = 0;
inline int checks_failed = 0;
inline int current_test_failures = 0;
inline std::vector<std::string> failure_log;

// Renders a value for a failure message when it can be streamed, and says so
// plainly when it cannot, rather than refusing to compile.
template <typename T>
std::string show(const T& value) {
    if constexpr (requires(std::ostringstream& out) { out << value; }) {
        std::ostringstream out;
        out << std::boolalpha << value;
        return out.str();
    } else {
        return "<not printable>";
    }
}

inline void record(bool ok, const char* file, int line, const std::string& detail) {
    ++checks_run;
    if (ok) { return; }
    ++checks_failed;
    ++current_test_failures;
    std::ostringstream out;
    out << "    " << file << ":" << line << "  " << detail;
    failure_log.push_back(out.str());
}

}  // namespace tempo_test

#define TEMPO_TEST_CAT_(a, b) a##b
#define TEMPO_TEST_CAT(a, b) TEMPO_TEST_CAT_(a, b)

#define TEST(name)                                                            \
    static void name();                                                       \
    static ::tempo_test::Registrar TEMPO_TEST_CAT(registrar_, name){#name, &name}; \
    static void name()

#define CHECK(expr)                                                           \
    ::tempo_test::record(static_cast<bool>(expr), __FILE__, __LINE__,         \
                         std::string("CHECK(") + #expr + ") is false")

// Operands are captured BY VALUE, deliberately. Binding a reference here is
// wrong: an expression like std::get<0>(snapshot().max_args).id reaches into a
// temporary through a function that returns a reference, and lifetime extension
// does not propagate through a function return -- the reference would dangle and
// the check would silently compare garbage. By value also guarantees each
// operand is evaluated exactly once, so CHECK_EQ(counter(), 1) is safe.
//
// One consequence worth knowing: a bare string literal decays to const char*, so
// CHECK_EQ("a", "a") would compare pointers. Make at least one side a
// std::string and the right operator== is chosen.
#define CHECK_BINARY(a, op, b)                                                \
    do {                                                                      \
        const auto tempo_test_a = (a);                                        \
        const auto tempo_test_b = (b);                                        \
        ::tempo_test::record(                                                 \
            tempo_test_a op tempo_test_b, __FILE__, __LINE__,                 \
            std::string(#a " " #op " " #b) + "  --  left = " +                \
                ::tempo_test::show(tempo_test_a) +                            \
                ", right = " + ::tempo_test::show(tempo_test_b));             \
    } while (false)

#define CHECK_EQ(a, b) CHECK_BINARY(a, ==, b)
#define CHECK_NE(a, b) CHECK_BINARY(a, !=, b)
#define CHECK_LT(a, b) CHECK_BINARY(a, <, b)
#define CHECK_LE(a, b) CHECK_BINARY(a, <=, b)
#define CHECK_GT(a, b) CHECK_BINARY(a, >, b)
#define CHECK_GE(a, b) CHECK_BINARY(a, >=, b)

#define CHECK_NEAR(a, b, tolerance)                                           \
    do {                                                                      \
        const double tempo_test_a = static_cast<double>(a);                   \
        const double tempo_test_b = static_cast<double>(b);                   \
        const double tempo_test_t = static_cast<double>(tolerance);           \
        ::tempo_test::record(                                                 \
            std::fabs(tempo_test_a - tempo_test_b) <= tempo_test_t,           \
            __FILE__, __LINE__,                                               \
            std::string(#a " ~= " #b) + "  --  " +                            \
                ::tempo_test::show(tempo_test_a) + " vs " +                   \
                ::tempo_test::show(tempo_test_b) + " (tolerance " +           \
                ::tempo_test::show(tempo_test_t) + ")");                      \
    } while (false)

#define CHECK_THROWS_AS(expr, exception_type)                                 \
    do {                                                                      \
        bool tempo_test_threw_right = false;                                  \
        bool tempo_test_threw_other = false;                                  \
        try {                                                                 \
            (void)(expr);                                                     \
        } catch (const exception_type&) {                                     \
            tempo_test_threw_right = true;                                    \
        } catch (...) {                                                       \
            tempo_test_threw_other = true;                                    \
        }                                                                     \
        ::tempo_test::record(tempo_test_threw_right, __FILE__, __LINE__,      \
                             std::string(#expr " should throw " #exception_type) + \
                                 (tempo_test_threw_other ? " (threw something else)" \
                                                         : " (did not throw)")); \
    } while (false)

#define CHECK_NOTHROW(expr)                                                   \
    do {                                                                      \
        bool tempo_test_ok = true;                                            \
        try {                                                                 \
            (void)(expr);                                                     \
        } catch (...) {                                                       \
            tempo_test_ok = false;                                            \
        }                                                                     \
        ::tempo_test::record(tempo_test_ok, __FILE__, __LINE__,               \
                             std::string(#expr " threw unexpectedly"));       \
    } while (false)

#define FAIL(message)                                                         \
    ::tempo_test::record(false, __FILE__, __LINE__, std::string(message))

int main() {
    using namespace tempo_test;

    int failed_tests = 0;
    for (const TestCase& test : registry()) {
        current_test_failures = 0;
        failure_log.clear();

        try {
            test.run();
        } catch (const std::exception& error) {
            record(false, __FILE__, __LINE__,
                   std::string("test threw: ") + error.what());
        } catch (...) {
            record(false, __FILE__, __LINE__, "test threw a non-std exception");
        }

        if (current_test_failures == 0) {
            std::printf("  ok    %s\n", test.name);
        } else {
            ++failed_tests;
            std::printf("  FAIL  %s\n", test.name);
            for (const std::string& line : failure_log) {
                std::printf("%s\n", line.c_str());
            }
        }
    }

    std::printf("%s: %d tests, %d checks, %d failed\n",
                failed_tests == 0 ? "PASS" : "FAIL",
                static_cast<int>(registry().size()), checks_run, checks_failed);
    return failed_tests == 0 ? 0 : 1;
}
