#pragma once

#if defined(_MSC_VER)
#if !defined(_MSVC_LANG) || _MSVC_LANG < 202002L
#error "tempo requires C++20 support"
#endif
#elif __cplusplus < 202002L
#error "tempo requires C++20 support"
#endif

#include <iostream>
#include <type_traits>
#include <chrono>
#include <atomic>
#include <concepts>
#include <exception>
#include <functional>
#include <tuple>
#include <source_location>
#include <utility>

#define TEMPO_CALLABLE(callable) ::tempo::Callable<&callable>
#define TEMPO_FUNCTION(function) ::tempo::Function<&function>
#define TEMPO_METHOD(method) ::tempo::Method<&method>
#define TEMPO_CALLABLE_PROFILER(callable) ::tempo::CallableProfiler<&callable>
#define TEMPO_CALLABLE_METRICS(callable) ::tempo::CallableMetrics<&callable>
#define TEMPO_PROFILE_CALL(profiler, ...) (profiler).call_at(::std::source_location::current() __VA_OPT__(,) __VA_ARGS__)
#define TEMPO_METRICS_CALL(metrics, ...) (metrics).call_at(::std::source_location::current() __VA_OPT__(,) __VA_ARGS__)

namespace tempo{
//-------------------------------------------------------------------
template <auto Value>
concept FunctionPointer =
    std::is_pointer_v<decltype(Value)> &&
    std::is_function_v<std::remove_pointer_t<decltype(Value)>>;

template <auto Value>
concept MethodPointer = std::is_member_function_pointer_v<decltype(Value)>;

template <auto Value>
concept SupportedCallable = FunctionPointer<Value> || MethodPointer<Value>;

namespace detail {

// Argümanları saklamak için imzadaki referansları soyuyoruz: std::tuple<const T&>
// ne varsayılan kurulabilir ne de yeniden atanabilir.
template <typename Tuple>
struct DecayedTuple;

template <typename... Ts>
struct DecayedTuple<std::tuple<Ts...>> {
    using Type = std::tuple<std::decay_t<Ts>...>;
};

// Argümanları ancak kopyalanabilir ve varsayılan kurulabilirlerse saklayabiliriz.
// Sadece taşınabilir (unique_ptr gibi) argümanlarda saklama sessizce kapanır.
template <typename Tuple>
struct ArgsAreStorable;

template <typename... Ts>
struct ArgsAreStorable<std::tuple<Ts...>>
    : std::bool_constant<(std::copy_constructible<std::decay_t<Ts>> && ...) &&
                         (std::default_initializable<std::decay_t<Ts>> && ...)> {};

// Bir üye fonksiyon işaretçisinin imzasını parçalıyoruz. Lambda ve functor'ların
// imzasını &F::operator() üzerinden buradan okuyoruz.
template <typename MemberPointer>
struct MemberSignature;

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...)> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = false;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...) const> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = true;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...) noexcept> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = false;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

template <typename Owner, typename ret, typename... args>
struct MemberSignature<ret (Owner::*)(args...) const noexcept> {
    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    static constexpr bool is_const = true;
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
};

} // namespace detail

// Çağrılabilir nesne: lambda, functor, std::function. Tek ve şablon olmayan bir
// operator() gerekiyor -- generic lambda ([](auto x){...}) ve operator()'ı
// overload edilmiş functor'lar burada eleniyor, çünkü çağrılmadan önce imzaları
// yok. Eleme sessiz değil: kısıt sağlanmadı hatası alırsınız.
template <typename F>
concept CallableObject =
    std::is_class_v<F> &&
    requires { &F::operator(); };

template<auto Func>
requires FunctionPointer<Func>
struct Function;

template <typename ret, typename... args, ret(*func_ptr)(args...)>
struct Function<func_ptr>{

    using  ReturnType = ret;
    using  ArgsType   = std::tuple<args...>;
    using  ClassType  = void;
    static constexpr bool is_member = false;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = false;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) + ... + 0);
    inline static std::atomic<unsigned int> call_count{0};

    // Parametreler forwarding reference: argüman func_ptr'ye kendi value
    // category'siyle, aradan kopya çıkmadan ulaşıyor. std::invocable kısıtı
    // hatayı std::invoke'un içinde değil çağrı yerinde tutuyor.
    template <typename... CallArgs>
        requires std::invocable<decltype(func_ptr), CallArgs...>
    ReturnType operator()(CallArgs&&... call_args) const {
        call_count++;
        return std::invoke(func_ptr, std::forward<CallArgs>(call_args)...);
    }

};

template<auto MethodValue>
requires MethodPointer<MethodValue>
struct Method;

template <typename ClassName,typename ret, typename...args , ret(ClassName::*method)(args...)>
struct Method<method>{

    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    using ClassType = ClassName;
    static constexpr bool is_member = true;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = false;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) +  ... +  0);
    inline static std::atomic<unsigned int> call_count{0};

    // Instance de forward ediliyor: std::invoke sayesinde ClassName&,
    // ClassName*, std::reference_wrapper ve akıllı işaretçiler de geçerli.
    template <typename Self, typename... CallArgs>
        requires std::invocable<decltype(method), Self, CallArgs...>
    ReturnType operator()(Self&& self, CallArgs&&... call_args) const {
        call_count++;
        return std::invoke(method, std::forward<Self>(self), std::forward<CallArgs>(call_args)...);
    }
};

template <typename ClassName,typename ret, typename...args , ret(ClassName::*method)(args...) const>
struct Method<method>{

    using ReturnType = ret;
    using ArgsType = std::tuple<args...>;
    using ClassType = ClassName;
    static constexpr bool is_member = true;
    static constexpr bool is_const_member = true;
    static constexpr bool is_functor = false;
    static constexpr auto arg_count = sizeof...(args);
    static constexpr auto total_arg_size = (sizeof(args) +  ... +  0);
    inline static std::atomic<unsigned int> call_count{0};

    template <typename Self, typename... CallArgs>
        requires std::invocable<decltype(method), Self, CallArgs...>
    ReturnType operator()(Self&& self, CallArgs&&... call_args) const {
        call_count++;
        return std::invoke(method, std::forward<Self>(self), std::forward<CallArgs>(call_args)...);
    }
};

template<auto CallableValue>
requires SupportedCallable<CallableValue>
struct CallableImplementation;

template<auto CallableValue>
requires FunctionPointer<CallableValue>
struct CallableImplementation<CallableValue> {
    using Type = Function<CallableValue>;
};

template<auto CallableValue>
requires MethodPointer<CallableValue>
struct CallableImplementation<CallableValue> {
    using Type = Method<CallableValue>;
};

template<auto CallableValue>
requires SupportedCallable<CallableValue>
struct Callable : CallableImplementation<CallableValue>::Type {
    using CallableType = typename CallableImplementation<CallableValue>::Type;
    using CallableType::operator();
};

// Function ve Method durum tutmaz, o yüzden şablon argümanı bir işaretçi
// (NTTP) olabiliyor. Lambda ve functor ise NESNEDİR: yakalama yapan bir lambda
// hiçbir zaman NTTP olamaz. Bu yüzden Functor tipe göre şablonlanır ve
// çağrılabilir nesnenin kendisini içinde taşır.
template <typename F>
requires CallableObject<F>
struct Functor {

    using SignatureType = detail::MemberSignature<decltype(&F::operator())>;
    using ReturnType = typename SignatureType::ReturnType;
    using ArgsType   = typename SignatureType::ArgsType;
    using ClassType  = F;

    // Örneği çağıran taraf ayrıca geçmediği için is_member false; nesne
    // wrapper'ın içinde duruyor.
    static constexpr bool is_member = false;
    static constexpr bool is_const_member = false;
    static constexpr bool is_functor = true;
    static constexpr bool is_const_callable = SignatureType::is_const;
    static constexpr auto arg_count = std::tuple_size_v<ArgsType>;
    static constexpr auto total_arg_size = SignatureType::total_arg_size;

    // Sayaç tipe bağlı. Her lambda İFADESİ kendi benzersiz closure tipini
    // ürettiği için bu lambda başına ayrı sayaç demek. Aynı tipten iki nesne
    // (iki std::function<int(int)> gibi) ise sayacı PAYLAŞIR.
    inline static std::atomic<unsigned int> call_count{0};

    // mutable: mutable lambda'ların operator()'ı const değil, ama Profiler ve
    // Metrics zinciri const üzerinden çağırıyor.
    mutable F target;

    template <typename... CallArgs>
        requires std::invocable<F&, CallArgs...>
    ReturnType operator()(CallArgs&&... call_args) const {
        call_count++;
        return std::invoke(target, std::forward<CallArgs>(call_args)...);
    }
};


// C++20 sonrasında TMP daha temiz
//
// Wrapper tipine göre şablonlanıyor (Callable<&f> ya da Functor<Lambda>), NTTP'ye
// göre değil: bir lambda şablon argümanı olamaz ama tipi olabilir.
template <typename WrapperType>
struct Profiler{

    using CallableType = WrapperType;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType = typename CallableType::ArgsType;

    using SourceLocation = std::source_location;
    inline static SourceLocation last_call_location{};
    static SourceLocation get_last_call_location() { return last_call_location; }
    
    CallableType callable;


    ReturnType call_at(SourceLocation location, auto&&... args) const
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        last_call_location = location;
        std::cout << "[CallableProfiler] Starting execution...\n";
        std::cout << "[CallableProfiler] Call location: " << location.file_name() << ":" << location.line() << "\n";
        std::cout << "[CallableProfiler] Caller function: " << location.function_name() << "\n";
        std::cout << "[CallableProfiler] Total size of args:" << CallableType::total_arg_size << " bytes\n";

        // Çağrıdan sonraki iş yıkıcıda çalışıyor; böylece çağrı ifadesini
        // doğrudan return edebiliyoruz. İsimli bir yerel değişken olmadığı için
        // dönüş değeri hiç kopyalanmıyor/taşınmıyor (garantili copy elision).
        [[maybe_unused]] const ReportOnExit report{};
        return callable(std::forward<decltype(args)>(args)...);
    }

    ReturnType operator()(auto&&... args) const
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return call_at(SourceLocation::current(), std::forward<decltype(args)>(args)...);
    }

private:
    struct ReportOnExit {
        int exceptions_on_entry = std::uncaught_exceptions();

        ~ReportOnExit() {
            if (std::uncaught_exceptions() != exceptions_on_entry) {
                return; // çağrı fırlattı, sayacı raporlama
            }
            std::cout << "[CallableProfiler] Call count: " << CallableType::call_count << "\n";
        }
    };
};

// Eski isim korunuyor: TEMPO_CALLABLE_PROFILER ve mevcut kod aynen çalışsın.
template <auto CallableValue>
requires SupportedCallable<CallableValue>
using CallableProfiler = Profiler<Callable<CallableValue>>;
//-------------------------------------------------------------------
// Ölçüm yapan taraf Profiler'a DELEGE ETMEZ. Profiler her çağrıda beş satır
// yazıyor; Metrics onun üzerinden çağırsaydı bu I/O ölçüm penceresinin içinde
// kalırdı ve tek bir çağrının "süresi" birkaç mikrosaniyelik cout maliyetiyle
// başlardı. Burada zamanlanan tek şey çağrının kendisi; raporlama saat
// durdurulduktan sonra yapılıyor.
template <typename WrapperType>
struct Metrics {

    using CallableType = WrapperType;
    using ReturnType = typename CallableType::ReturnType;
    using ArgsType   = typename CallableType::ArgsType;
    using SourceLocation = std::source_location;
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;

    static constexpr bool tracks_args = detail::ArgsAreStorable<ArgsType>::value;

    // İmzadaki referanslar soyulmuş hâli: saklanabilir, atanabilir.
    using StoredArgsType = std::conditional_t<
        tracks_args,
        typename detail::DecayedTuple<ArgsType>::Type,
        std::tuple<>>;

    inline static Duration total_duration{0};
    inline static Duration max_duration{0};
    inline static Duration min_duration{0};
    inline static StoredArgsType min_args{};
    inline static StoredArgsType max_args{};
    inline static auto& call_count = CallableType::call_count;
    inline static SourceLocation last_call_location{};

    static SourceLocation get_last_call_location() { return last_call_location; }

    // Wrapper'ı doğrudan tutuyoruz: Functor durumunda çağrılabilir nesnenin
    // kendisi burada yaşıyor, her çağrıda yeniden kurulamaz.
    CallableType callable;

    // Dikkat: burada bilerek forward ETMİYORUZ. Argümanlar asıl çağrıya forward
    // edileceği için oradan taşınmış (moved-from) olabilirler; snapshot'ı çağrı
    // ÖNCESİNDE ve kopyalayarak alıyoruz ki sakladığımız değerler doğru olsun.
    static StoredArgsType make_args_snapshot(const auto&... args) {
        if constexpr (!tracks_args) {
            return StoredArgsType{};
        }
        else if constexpr (CallableType::is_member) {
            return make_args_snapshot_without_instance(args...);
        }
        else {
            return StoredArgsType{args...};
        }
    }

    static void reset() {
        call_count.store(0);
        total_duration = Duration{0};
        max_duration = Duration{0};
        min_duration = Duration{0};
        min_args = StoredArgsType{};
        max_args = StoredArgsType{};
        last_call_location = SourceLocation{};
    }

    ReturnType call_at(SourceLocation location, auto&&... args) const
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        last_call_location = location;

        // Snapshot çağrıdan önce alınıyor (kopya), asıl çağrıya orijinaller
        // forward ediliyor. İki tarafa birden forward etmek moved-from değer
        // saklamaya yol açardı.
        StoredArgsType snapshot = make_args_snapshot(args...);

        // Saat, guard kurulurken başlıyor ve yıkıcının İLK satırında duruyor;
        // arada yalnızca çağrının kendisi var. Raporlama saat durduktan sonra.
        // Dönüş değeri isimli bir yerele uğramadığı için hiç kopyalanmıyor,
        // taşınamayan tipler bile geçiyor.
        [[maybe_unused]] const RecordOnExit record{location, snapshot};
        return callable(std::forward<decltype(args)>(args)...);
    }

    ReturnType operator()(auto&&... args) const
        requires std::invocable<const CallableType&, decltype(args)...>
    {
        return call_at(SourceLocation::current(), std::forward<decltype(args)>(args)...);
    }

    StoredArgsType get_minimizers() const {return min_args;}
    StoredArgsType get_maximizers() const {return max_args;}

private:
    template <typename Instance, typename... MethodArgs>
    static StoredArgsType make_args_snapshot_without_instance(const Instance&, const MethodArgs&... arg) {
        return StoredArgsType{arg...};
    }

    struct RecordOnExit {
        SourceLocation location;
        StoredArgsType& snapshot;
        int exceptions_on_entry = std::uncaught_exceptions();
        Clock::time_point start = Clock::now();

        ~RecordOnExit() {
            // Saati her şeyden önce durduruyoruz: buradan sonrası ölçüme dahil değil.
            const Duration duration = Clock::now() - start;

            if (std::uncaught_exceptions() != exceptions_on_entry) {
                return; // çağrı fırlattı, yarım kalan süreyi metriklere yazma
            }

            total_duration += duration;

            const bool is_new_max = duration > max_duration;
            const bool is_new_min = (CallableType::call_count == 1) || duration < min_duration;

            if (is_new_max) { max_duration = duration; }
            if (is_new_min) { min_duration = duration; }

            if constexpr (tracks_args) {
                // Snapshot'ı yalnızca gerçekten gerekiyorsa taşıyoruz; ikisi
                // birden tetiklenirse tek kopya yetiyor.
                if (is_new_max && is_new_min) {
                    max_args = snapshot;
                    min_args = std::move(snapshot);
                }
                else if (is_new_max) { max_args = std::move(snapshot); }
                else if (is_new_min) { min_args = std::move(snapshot); }
            }

            // Buradan aşağısı saat durduktan sonra çalışıyor, ölçümü kirletmiyor.
            std::cout << "[CallableMetrics] Callable ran. Took: " << duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Call location: " << location.file_name() << ":" << location.line() << "\n";
            std::cout << "[CallableMetrics] Caller function: " << location.function_name() << "\n";
            std::cout << "[CallableMetrics] Call count: " << CallableType::call_count << "\n";
            std::cout << "[CallableMetrics] Total size of args: " << CallableType::total_arg_size << " bytes\n";
            std::cout << "[CallableMetrics] Total time spent : " << total_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Min time : " << min_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Max time : " << max_duration.count() << " ms\n";
            std::cout << "[CallableMetrics] Average time : " << (total_duration.count() / CallableType::call_count) << " ms\n";
        }
    };

    };

// Eski isim korunuyor: TEMPO_CALLABLE_METRICS ve mevcut kod aynen çalışsın.
template <auto CallableValue>
requires SupportedCallable<CallableValue>
using CallableMetrics = Metrics<Callable<CallableValue>>;

//-------------------------------------------------------------------
// Lambda ve functor'lar için fabrikalar.
//
// Fonksiyon işaretçilerinde tip adını makroyla yazabiliyoruz
// (TEMPO_CALLABLE_METRICS(f)) çünkü &f bir şablon argümanı. Bir lambda için bu
// mümkün değil: nesneyi geçmek ve tipini çıkarım yoluyla almak zorundayız.
template <typename F>
requires CallableObject<std::decay_t<F>>
auto wrap(F&& target) {
    return Functor<std::decay_t<F>>{std::forward<F>(target)};
}

template <typename F>
requires CallableObject<std::decay_t<F>>
auto profile(F&& target) {
    return Profiler<Functor<std::decay_t<F>>>{wrap(std::forward<F>(target))};
}

template <typename F>
requires CallableObject<std::decay_t<F>>
auto measure(F&& target) {
    return Metrics<Functor<std::decay_t<F>>>{wrap(std::forward<F>(target))};
}
//-------------------------------------------------------------------

template <typename ClassType>
concept IsClass = std::is_class_v<ClassType>;

template <typename ClassType>
requires IsClass<ClassType>
struct ConstructorProfiler{

    inline static std::atomic<unsigned int> obj_count{0};

    // Argümanlar yapıcıya forward ediliyor: taşınabilir argümanlar taşınıyor,
    // prvalue döndürüldüğü için nesne doğrudan çağıranın yerinde kuruluyor
    // (garantili copy elision) -- kopyalanamayan/taşınamayan tipler de çalışıyor.
    template <typename... Args>
        requires std::constructible_from<ClassType, Args...>
    ClassType operator() (Args&&... args) const {
        [[maybe_unused]] const CountOnSuccess counter{};
        return ClassType(std::forward<Args>(args)...);
        };

private:
    struct CountOnSuccess {
        int exceptions_on_entry = std::uncaught_exceptions();

        ~CountOnSuccess() {
            if (std::uncaught_exceptions() == exceptions_on_entry) {
                obj_count++;
            }
        }
    };
    };
 };
