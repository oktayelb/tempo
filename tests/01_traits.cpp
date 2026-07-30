// 01 — Signature introspection
//
// Nearly everything here is a static_assert: if the traits are wrong the file
// does not compile, which is the strongest form of test this layer can have.
// The TEST bodies exist so the runner reports something meaningful.

#include "tempo.hpp"
#include "tempo_test.hpp"

#include <functional>
#include <memory>
#include <string>

namespace {

int free_add(int a, int b) { return a + b; }
void free_void() {}
double free_mixed(const std::string&, char, long long) { return 0.0; }

struct Service {
    int handle(int a, double b) { return a + static_cast<int>(b); }
    int inspect(int a) const { return a; }
    void mutate() {}
};

struct Functor {
    int operator()(int a, int b) const { return a * b; }
};

struct MutableFunctor {
    int count = 0;
    int operator()(int a) { return ++count + a; }
};

}  // namespace

// ---------- free functions ----------
using Add = TEMPO_CALLABLE(free_add);
static_assert(std::is_same_v<Add::ReturnType, int>);
static_assert(std::is_same_v<Add::ArgsType, std::tuple<int, int>>);
static_assert(std::is_same_v<Add::ClassType, void>);
static_assert(Add::arg_count == 2);
static_assert(!Add::is_member);
static_assert(!Add::is_const_member);
static_assert(!Add::is_functor);
static_assert(Add::total_arg_size == sizeof(int) * 2);

using Void = TEMPO_CALLABLE(free_void);
static_assert(std::is_same_v<Void::ReturnType, void>);
static_assert(Void::arg_count == 0);
static_assert(Void::total_arg_size == 0);
static_assert(std::is_same_v<Void::ArgsType, std::tuple<>>);

using Mixed = TEMPO_CALLABLE(free_mixed);
static_assert(std::is_same_v<Mixed::ReturnType, double>);
static_assert(std::is_same_v<Mixed::ArgsType,
                             std::tuple<const std::string&, char, long long>>);
static_assert(Mixed::arg_count == 3);

// ---------- member functions ----------
using Handle = TEMPO_CALLABLE(Service::handle);
static_assert(std::is_same_v<Handle::ReturnType, int>);
static_assert(std::is_same_v<Handle::ArgsType, std::tuple<int, double>>);
static_assert(std::is_same_v<Handle::ClassType, Service>);
static_assert(Handle::is_member);
static_assert(!Handle::is_const_member);
static_assert(Handle::arg_count == 2);

using Inspect = TEMPO_CALLABLE(Service::inspect);
static_assert(Inspect::is_member);
static_assert(Inspect::is_const_member);      // the const overload specialization
static_assert(Inspect::arg_count == 1);

using Mutate = TEMPO_CALLABLE(Service::mutate);
static_assert(std::is_same_v<Mutate::ReturnType, void>);
static_assert(Mutate::is_member);
static_assert(Mutate::arg_count == 0);

// ---------- functors and lambdas ----------
using Fn = tempo::Functor<Functor>;
static_assert(std::is_same_v<Fn::ReturnType, int>);
static_assert(std::is_same_v<Fn::ArgsType, std::tuple<int, int>>);
static_assert(std::is_same_v<Fn::ClassType, Functor>);
static_assert(Fn::is_functor);
static_assert(!Fn::is_member);          // the object lives inside the wrapper
static_assert(Fn::is_const_callable);   // operator() is const
static_assert(Fn::arg_count == 2);

using MutFn = tempo::Functor<MutableFunctor>;
static_assert(!MutFn::is_const_callable);
static_assert(MutFn::arg_count == 1);

// A lambda's closure type works the same way. Note the decay: decltype of a
// constexpr variable is const-qualified, and Functor stores its target as
// `mutable F`, which cannot also be const. tempo::wrap/measure/profile decay for
// you; naming Functor directly is the only place this comes up.
constexpr auto lambda = [](double x, int y) { return x * y; };
using Lam = tempo::Functor<std::decay_t<decltype(lambda)>>;
static_assert(std::is_same_v<Lam::ReturnType, double>);
static_assert(std::is_same_v<Lam::ArgsType, std::tuple<double, int>>);
static_assert(Lam::is_functor);
static_assert(Lam::is_const_callable);

// Each lambda expression has its own type, so two identical-looking lambdas are
// distinct wrappers with distinct counters.
constexpr auto twin_a = [](int x) { return x; };
constexpr auto twin_b = [](int x) { return x; };
static_assert(!std::is_same_v<decltype(twin_a), decltype(twin_b)>);
static_assert(!std::is_same_v<tempo::Functor<std::decay_t<decltype(twin_a)>>,
                              tempo::Functor<std::decay_t<decltype(twin_b)>>>);

// std::function is a class with one non-template operator(), so it qualifies --
// and is exactly why two std::function<int(int)> share a counter.
static_assert(tempo::callable_traits::CallableObject<std::function<int(int)>>);
static_assert(std::is_same_v<tempo::Functor<std::function<int(int)>>::ReturnType, int>);

// ---------- the concepts reject what they should ----------
static_assert(tempo::callable_traits::FunctionPointer<&free_add>);
static_assert(!tempo::callable_traits::MethodPointer<&free_add>);
static_assert(tempo::callable_traits::MethodPointer<&Service::handle>);
static_assert(!tempo::callable_traits::FunctionPointer<&Service::handle>);
static_assert(tempo::callable_traits::CallablePointer<&free_add>);
static_assert(tempo::callable_traits::CallablePointer<&Service::handle>);

struct NotCallable { int value; };
static_assert(!tempo::callable_traits::CallableObject<NotCallable>);
static_assert(!tempo::callable_traits::CallableObject<int>);

// A generic lambda has no signature until called, so it must be rejected.
constexpr auto generic = [](auto x) { return x; };
static_assert(!tempo::callable_traits::CallableObject<decltype(generic)>);

// An overloaded operator() is ambiguous for &F::operator(), so it is rejected.
struct Overloaded {
    int operator()(int) const { return 0; }
    int operator()(double) const { return 1; }
};
static_assert(!tempo::callable_traits::CallableObject<Overloaded>);

// ---------- Metrics and Profiler forward the traits ----------
using AddMetrics = TEMPO_CALLABLE_METRICS(free_add);
static_assert(std::is_same_v<AddMetrics::ReturnType, int>);
static_assert(std::is_same_v<AddMetrics::ArgsType, std::tuple<int, int>>);
static_assert(AddMetrics::tracks_args);
static_assert(std::is_same_v<AddMetrics::StoredArgsType, std::tuple<int, int>>);

// Reference parameters are decayed for storage: tuple<const string&> is neither
// default constructible nor assignable.
using MixedMetrics = TEMPO_CALLABLE_METRICS(free_mixed);
static_assert(MixedMetrics::tracks_args);
static_assert(std::is_same_v<MixedMetrics::StoredArgsType,
                             std::tuple<std::string, char, long long>>);

// A move-only parameter cannot be stored, so tracking switches itself off
// instead of failing to compile.
namespace {
int consume(std::unique_ptr<int> owned) { return owned ? *owned : -1; }
}
using ConsumeMetrics = TEMPO_CALLABLE_METRICS(consume);
static_assert(!ConsumeMetrics::tracks_args);
static_assert(std::is_same_v<ConsumeMetrics::StoredArgsType, std::tuple<>>);

// The instance is not part of a member's stored arguments.
using HandleMetrics = TEMPO_CALLABLE_METRICS(Service::handle);
static_assert(std::tuple_size_v<HandleMetrics::StoredArgsType> == 2);

// The clock must be monotonic. This is the bug that made durations go negative
// under NTP adjustment: on libstdc++ high_resolution_clock is system_clock.
static_assert(AddMetrics::Clock::is_steady);
static_assert(std::is_same_v<AddMetrics::Clock, std::chrono::steady_clock>);

TEST(traits_are_compile_time) {
    // Everything above is a static_assert; reaching here means they all held.
    CHECK(true);
}

TEST(constructor_profiler_can_construct) {
    struct Point {
        Point(int, int) {}
        explicit Point(const std::string&) {}
    };
    using Maker = tempo::ConstructorProfiler<Point>;

    static_assert(Maker::can_construct<int, int>);
    static_assert(Maker::can_construct<const std::string&>);
    static_assert(!Maker::can_construct<int>);
    static_assert(!Maker::can_construct<>);
    static_assert(!Maker::can_construct<int, int, int>);
    CHECK(true);
}
