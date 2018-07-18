#ifndef OPTIMIZER_HPP
#define OPTIMIZER_HPP

#include <tuple>
#include <functional>
#include <limits>
#include "common.hpp"

namespace libnest2d { namespace opt {

using std::forward;
using std::tuple;
using std::get;
using std::tuple_element;

/// A Type trait for upper and lower limit of a numeric type.
template<class T, class B = void >
struct limits {
    inline static T min() { return std::numeric_limits<T>::min(); }
    inline static T max() { return std::numeric_limits<T>::max(); }
};

template<class T>
struct limits<T, enable_if_t<std::numeric_limits<T>::has_infinity, void>> {
    inline static T min() { return -std::numeric_limits<T>::infinity(); }
    inline static T max() { return std::numeric_limits<T>::infinity(); }
};

/// An interval of possible input values for optimization
template<class T>
class Bound {
    T min_;
    T max_;
public:
    Bound(const T& min = limits<T>::min(),
          const T& max = limits<T>::max()): min_(min), max_(max) {}
    inline const T min() const BP2D_NOEXCEPT { return min_; }
    inline const T max() const BP2D_NOEXCEPT { return max_; }
};

/**
 * Helper function to make a Bound object with its type deduced automatically.
 */
template<class T>
inline Bound<T> bound(const T& min, const T& max) { return Bound<T>(min, max); }

/**
 * This is the type of an input tuple for the object function. It holds the
 * values and their type in each dimension.
 */
template<class...Args> using Input = tuple<Args...>;

template<class...Args>
inline tuple<Args...> initvals(Args...args) { return std::make_tuple(args...); }

/**
 * @brief Helper class to be able to loop over a parameter pack's elements.
 */
class metaloop {
// The implementation is based on partial struct template specializations.
// Basically we need a template type that is callable and takes an integer
// non-type template parameter which can be used to implement recursive calls.
//
// C++11 will not allow the usage of a plain template function that is why we
// use struct with overloaded call operator. At the same time C++11 prohibits
// partial template specialization with a non type parameter such as int. We
// need to wrap that in a type (see metaloop::Int).

/*
 * A helper alias to create integer values wrapped as a type. It is nessecary
 * because a non type template parameter (such as int) would be prohibited in
 * a partial specialization. Also for the same reason we have to use a class
 * _Metaloop instead of a simple function as a functor. A function cannot be
 * partially specialized in a way that is neccesary for this trick.
 */
template<int N> using Int = std::integral_constant<int, N>;

/*
 * Helper class to implement in-place functors.
 *
 * We want to be able to use inline functors like a lambda to keep the code
 * as clear as possible.
 */
template<int N, class Fn> class MapFn {
    Fn&& fn_;
public:

    // It takes the real functor that can be specified in-place but only
    // with C++14 because the second parameter's type will depend on the
    // type of the parameter pack element that is processed. In C++14 we can
    // specify this second parameter type as auto in the lamda parameter list.
    inline MapFn(Fn&& fn): fn_(forward<Fn>(fn)) {}

    template<class T> void operator ()(T&& pack_element) {
        // We provide the index as the first parameter and the pack (or tuple)
        // element as the second parameter to the functor.
        fn_(N, forward<T>(pack_element));
    }
};

/*
 * Implementation of the template loop trick.
 * We create a mechanism for looping over a parameter pack in compile time.
 * \tparam Idx is the loop index which will be decremented at each recursion.
 * \tparam Args The parameter pack that will be processed.
 *
 */
template <typename Idx, class...Args>
class _MetaLoop {};

// Implementation for the first element of Args...
template <class...Args>
class _MetaLoop<Int<0>, Args...> {
public:

    const static BP2D_CONSTEXPR int N = 0;
    const static BP2D_CONSTEXPR int ARGNUM = sizeof...(Args)-1;

    template<class Tup, class Fn>
    void run( Tup&& valtup, Fn&& fn) {
        MapFn<ARGNUM-N, Fn> {forward<Fn>(fn)} (get<ARGNUM-N>(valtup));
    }
};

// Implementation for the N-th element of Args...
template <int N, class...Args>
class _MetaLoop<Int<N>, Args...> {
public:

    const static BP2D_CONSTEXPR int ARGNUM = sizeof...(Args)-1;

    template<class Tup, class Fn>
    void run(Tup&& valtup, Fn&& fn) {
        MapFn<ARGNUM-N, Fn> {forward<Fn>(fn)} (std::get<ARGNUM-N>(valtup));

        // Recursive call to process the next element of Args
        _MetaLoop<Int<N-1>, Args...> ().run(forward<Tup>(valtup),
                                            forward<Fn>(fn));
    }
};

/*
 * Instantiation: We must instantiate the template with the last index because
 * the generalized version calls the decremented instantiations recursively.
 * Once the instantiation with the first index is called, the terminating
 * version of run is called which does not call itself anymore.
 *
 * If you are utterly annoyed, at least you have learned a super crazy
 * functional metaprogramming pattern.
 */
template<class...Args>
using MetaLoop = _MetaLoop<Int<sizeof...(Args)-1>, Args...>;

public:

/**
 * \brief The final usable function template.
 *
 * This is similar to what varags was on C but in compile time C++11.
 * You can call:
 * apply(<the mapping function>, <arbitrary number of arguments of any type>);
 * For example:
 *
 *      struct mapfunc {
 *          template<class T> void operator()(int N, T&& element) {
 *              std::cout << "The value of the parameter "<< N <<": "
 *                        << element << std::endl;
 *          }
 *      };
 *
 *      apply(mapfunc(), 'a', 10, 151.545);
 *
 * C++14:
 *      apply([](int N, auto&& element){
 *          std::cout << "The value of the parameter "<< N <<": "
 *                        << element << std::endl;
 *      }, 'a', 10, 151.545);
 *
 * This yields the output:
 * The value of the parameter 0: a
 * The value of the parameter 1: 10
 * The value of the parameter 2: 151.545
 *
 * As an addition, the function can be called with a tuple as the second
 * parameter holding the arguments instead of a parameter pack.
 *
 */
template<class...Args, class Fn>
inline static void apply(Fn&& fn, Args&&...args) {
    MetaLoop<Args...>().run(tuple<Args&&...>(forward<Args>(args)...),
                            forward<Fn>(fn));
}

/// The version of apply with a tuple rvalue reference.
template<class...Args, class Fn>
inline static void apply(Fn&& fn, tuple<Args...>&& tup) {
    MetaLoop<Args...>().run(std::move(tup), forward<Fn>(fn));
}

/// The version of apply with a tuple lvalue reference.
template<class...Args, class Fn>
inline static void apply(Fn&& fn, tuple<Args...>& tup) {
    MetaLoop<Args...>().run(tup, forward<Fn>(fn));
}

/// The version of apply with a tuple const reference.
template<class...Args, class Fn>
inline static void apply(Fn&& fn, const tuple<Args...>& tup) {
    MetaLoop<Args...>().run(tup, forward<Fn>(fn));
}

/**
 * Call a function with its arguments encapsualted in a tuple.
 */
template<class Fn, class Tup, std::size_t...Is>
inline static auto
callFunWithTuple(Fn&& fn, Tup&& tup, index_sequence<Is...>) ->
    decltype(fn(std::get<Is>(tup)...))
{
    return fn(std::get<Is>(tup)...);
}

};

/**
 * @brief Specific optimization methods for which a default optimizer
 * implementation can be instantiated.
 */
enum class Method {
    L_SIMPLEX,
    L_SUBPLEX,
    G_GENETIC,
    //...
};

/**
 * @brief Info about result of an optimization. These codes are exactly the same
 * as the nlopt codes for convinience.
 */
enum ResultCodes {
    FAILURE = -1, /* generic failure code */
    INVALID_ARGS = -2,
    OUT_OF_MEMORY = -3,
    ROUNDOFF_LIMITED = -4,
    FORCED_STOP = -5,
    SUCCESS = 1, /* generic success code */
    STOPVAL_REACHED = 2,
    FTOL_REACHED = 3,
    XTOL_REACHED = 4,
    MAXEVAL_REACHED = 5,
    MAXTIME_REACHED = 6
};

/**
 * \brief A type to hold the complete result of the optimization.
 */
template<class...Args>
struct Result {
    ResultCodes resultcode;
    std::tuple<Args...> optimum;
    double score;
};

/**
 * @brief The stop limit can be specified as the absolute error or as the
 * relative error, just like in nlopt.
 */
enum class StopLimitType {
    ABSOLUTE,
    RELATIVE
};

/**
 * @brief A type for specifying the stop criteria.
 */
struct StopCriteria {

    /// Relative or absolute termination error
    StopLimitType type = StopLimitType::RELATIVE;

    /// The error value that is interpredted depending on the type property.
    double stoplimit = 0.0001;

    unsigned max_iterations = 0;
};

/**
 * \brief The Optimizer base class with CRTP pattern.
 */
template<class Subclass>
class Optimizer {
protected:
    enum class OptDir{
        MIN,
        MAX
    } dir_;

    StopCriteria stopcr_;

public:

    inline explicit Optimizer(const StopCriteria& scr = {}): stopcr_(scr) {}

    /**
     * \brief Optimize for minimum value of the provided objectfunction.
     * \param objectfunction The function that will be searched for the minimum
     * return value.
     * \param initvals A tuple with the initial values for the search
     * \param bounds A parameter pack with the bounds for each dimension.
     * \return Returns a Result<Args...> structure.
     * An example call would be:
     *     auto result = opt.optimize_min(
     *           [](std::tuple<double> x) // object function
     *           {
     *               return std::pow(std::get<0>(x), 2);
     *           },
     *           std::make_tuple(-0.5),  // initial value
     *           {-1.0, 1.0}           // search space bounds
     *       );
     */
    template<class Func, class...Args>
    inline Result<Args...> optimize_min(Func&& objectfunction,
                                        Input<Args...> initvals,
                                        Bound<Args>... bounds)
    {
        dir_ = OptDir::MIN;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    forward<Func>(objectfunction), initvals, bounds... );
    }

    template<class Func, class...Args>
    inline Result<Args...> optimize_min(Func&& objectfunction,
                                        Input<Args...> initvals)
    {
        dir_ = OptDir::MIN;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    objectfunction, initvals, Bound<Args>()... );
    }

    template<class...Args, class Func>
    inline Result<Args...> optimize_min(Func&& objectfunction)
    {
        dir_ = OptDir::MIN;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    objectfunction,
                    Input<Args...>(),
                    Bound<Args>()... );
    }

    /// Same as optimize_min but optimizes for maximum function value.
    template<class Func, class...Args>
    inline Result<Args...> optimize_max(Func&& objectfunction,
                                        Input<Args...> initvals,
                                        Bound<Args>... bounds)
    {
        dir_ = OptDir::MAX;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    objectfunction, initvals, bounds... );
    }

    template<class Func, class...Args>
    inline Result<Args...> optimize_max(Func&& objectfunction,
                                        Input<Args...> initvals)
    {
        dir_ = OptDir::MAX;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    objectfunction, initvals, Bound<Args>()... );
    }

    template<class...Args, class Func>
    inline Result<Args...> optimize_max(Func&& objectfunction)
    {
        dir_ = OptDir::MAX;
        return static_cast<Subclass*>(this)->template optimize<Func, Args...>(
                    objectfunction,
                    Input<Args...>(),
                    Bound<Args>()... );
    }

};

// Just to be able to instantiate an unimplemented method and generate compile
// error.
template<class T = void>
class DummyOptimizer : public Optimizer<DummyOptimizer<T>> {
    friend class Optimizer<DummyOptimizer<T>>;

public:
    DummyOptimizer() {
        static_assert(always_false<T>::value, "Optimizer unimplemented!");
    }

    template<class Func, class...Args>
    Result<Args...> optimize(Func&& func,
                             std::tuple<Args...> initvals,
                             Bound<Args>... args)
    {
        return Result<Args...>();
    }
};

// Specializing this struct will tell what kind of optimizer to generate for
// a given method
template<Method m> struct OptimizerSubclass { using Type = DummyOptimizer<>; };

/// Optimizer type based on the method provided in parameter m.
template<Method m> using TOptimizer = typename OptimizerSubclass<m>::Type;

/// Global optimizer with an explicitly specified local method.
template<Method m>
inline TOptimizer<m> GlobalOptimizer(Method, const StopCriteria& scr = {})
{ // Need to be specialized in order to do anything useful.
    return TOptimizer<m>(scr);
}

}
}

#endif // OPTIMIZER_HPP
