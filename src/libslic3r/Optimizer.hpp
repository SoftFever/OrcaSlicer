#ifndef NLOPTOPTIMIZER_HPP
#define NLOPTOPTIMIZER_HPP

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#endif
#include <nlopt.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <utility>
#include <tuple>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <cassert>

namespace Slic3r { namespace opt {

// A type to hold the complete result of the optimization.
template<size_t N> struct Result {
    int resultcode;
    std::array<double, N> optimum;
    double score;
};

// An interval of possible input values for optimization
class Bound {
    double m_min, m_max;

public:
    Bound(double min = std::numeric_limits<double>::min(),
          double max = std::numeric_limits<double>::max())
        : m_min(min), m_max(max)
    {}

    double min() const noexcept { return m_min; }
    double max() const noexcept { return m_max; }
};

// Helper types for optimization function input and bounds
template<size_t N> using Input = std::array<double, N>;
template<size_t N> using Bounds = std::array<Bound, N>;

// A type for specifying the stop criteria. Setter methods can be concatenated
class StopCriteria {

    // If the absolute value difference between two scores.
    double m_abs_score_diff = std::nan("");

    // If the relative value difference between two scores.
    double m_rel_score_diff = std::nan("");

    // Stop if this value or better is found.
    double m_stop_score = std::nan("");

    // A predicate that if evaluates to true, the optimization should terminate
    // and the best result found prior to termination should be returned.
    std::function<bool()> m_stop_condition = [] { return false; };

    // The max allowed number of iterations.
    unsigned m_max_iterations = 0;

public:

    StopCriteria & abs_score_diff(double val)
    {
        m_abs_score_diff = val; return *this;
    }

    double abs_score_diff() const { return m_abs_score_diff; }

    StopCriteria & rel_score_diff(double val)
    {
        m_rel_score_diff = val; return *this;
    }

    double rel_score_diff() const { return m_rel_score_diff; }

    StopCriteria & stop_score(double val)
    {
        m_stop_score = val; return *this;
    }

    double stop_score() const { return m_stop_score; }

    StopCriteria & max_iterations(double val)
    {
        m_max_iterations = val; return *this;
    }

    double max_iterations() const { return m_max_iterations; }

    template<class Fn> StopCriteria & stop_condition(Fn &&cond)
    {
        m_stop_condition = cond; return *this;
    }

    bool stop_condition() { return m_stop_condition(); }
};

// Helper class to use optimization methods involving gradient.
template<size_t N> struct ScoreGradient {
    double score;
    std::optional<std::array<double, N>> gradient;

    ScoreGradient(double s, const std::array<double, N> &grad)
        : score{s}, gradient{grad}
    {}
};

// Helper to be used in static_assert.
template<class T> struct always_false { enum { value = false }; };

// Basic interface to optimizer object
template<class Method, class Enable = void> class Optimizer {
public:

    Optimizer(const StopCriteria &)
    {
        static_assert (always_false<Method>::value,
                       "Optimizer unimplemented for given method!");
    }

    Optimizer<Method> &to_min() { return *this; }
    Optimizer<Method> &to_max() { return *this; }
    Optimizer<Method> &set_criteria(const StopCriteria &) { return *this; }
    StopCriteria get_criteria() const { return {}; };

    template<class Func, size_t N>
    Result<N> optimize(Func&& func,
                       const Input<N> &initvals,
                       const Bounds<N>& bounds) { return {}; }

    // optional for randomized methods:
    void seed(long /*s*/) {}
};

namespace detail {

// Helper types for NLopt algorithm selection in template contexts
template<nlopt_algorithm alg> struct NLoptAlg {};

// NLopt can combine multiple algorithms if one is global an other is a local
// method. This is how template specializations can be informed about this fact.
template<nlopt_algorithm gl_alg, nlopt_algorithm lc_alg = NLOPT_LN_NELDERMEAD>
struct NLoptAlgComb {};

template<class M> struct IsNLoptAlg {
    static const constexpr bool value = false;
};

template<nlopt_algorithm a> struct IsNLoptAlg<NLoptAlg<a>> {
    static const constexpr bool value = true;
};

template<nlopt_algorithm a1, nlopt_algorithm a2>
struct IsNLoptAlg<NLoptAlgComb<a1, a2>> {
    static const constexpr bool value = true;
};

template<class M, class T = void>
using NLoptOnly = std::enable_if_t<IsNLoptAlg<M>::value, T>;

// Helper to convert C style array to std::array. The copy should be optimized
// away with modern compilers.
template<size_t N, class T> auto to_arr(const T *a)
{
    std::array<T, N> r;
    std::copy(a, a + N, std::begin(r));
    return r;
}

template<size_t N, class T> auto to_arr(const T (&a) [N])
{
    return to_arr<N>(static_cast<const T *>(a));
}

enum class OptDir { MIN, MAX }; // Where to optimize

struct NLopt { // Helper RAII class for nlopt_opt
    nlopt_opt ptr = nullptr;

    template<class...A> explicit NLopt(A&&...a)
    {
        ptr = nlopt_create(std::forward<A>(a)...);
    }

    NLopt(const NLopt&) = delete;
    NLopt(NLopt&&) = delete;
    NLopt& operator=(const NLopt&) = delete;
    NLopt& operator=(NLopt&&) = delete;

    ~NLopt() { nlopt_destroy(ptr); }
};

template<class Method> class NLoptOpt {};

// Optimizers based on NLopt.
template<nlopt_algorithm alg> class NLoptOpt<NLoptAlg<alg>> {
protected:
    StopCriteria m_stopcr;
    OptDir m_dir;

    template<class Fn> using TOptData =
        std::tuple<std::remove_reference_t<Fn>*, NLoptOpt*, nlopt_opt>;

    template<class Fn, size_t N>
    static double optfunc(unsigned n, const double *params,
                          double *gradient,
                          void *data)
    {
        assert(n >= N);

        auto tdata = static_cast<TOptData<Fn>*>(data);

        if (std::get<1>(*tdata)->m_stopcr.stop_condition())
            nlopt_force_stop(std::get<2>(*tdata));

        auto fnptr  = std::get<0>(*tdata);
        auto funval = to_arr<N>(params);

        double scoreval = 0.;
        using RetT = decltype((*fnptr)(funval));
        if constexpr (std::is_convertible_v<RetT, ScoreGradient<N>>) {
            ScoreGradient<N> score = (*fnptr)(funval);
            for (size_t i = 0; i < n; ++i) gradient[i] = (*score.gradient)[i];
            scoreval = score.score;
        } else {
            scoreval = (*fnptr)(funval);
        }

        return scoreval;
    }

    template<size_t N>
    void set_up(NLopt &nl, const Bounds<N>& bounds)
    {
        std::array<double, N> lb, ub;

        for (size_t i = 0; i < N; ++i) {
            lb[i] = bounds[i].min();
            ub[i] = bounds[i].max();
        }

        nlopt_set_lower_bounds(nl.ptr, lb.data());
        nlopt_set_upper_bounds(nl.ptr, ub.data());

        double abs_diff = m_stopcr.abs_score_diff();
        double rel_diff = m_stopcr.rel_score_diff();
        double stopval = m_stopcr.stop_score();
        if(!std::isnan(abs_diff)) nlopt_set_ftol_abs(nl.ptr, abs_diff);
        if(!std::isnan(rel_diff)) nlopt_set_ftol_rel(nl.ptr, rel_diff);
        if(!std::isnan(stopval))  nlopt_set_stopval(nl.ptr, stopval);

        if(this->m_stopcr.max_iterations() > 0)
            nlopt_set_maxeval(nl.ptr, this->m_stopcr.max_iterations());
    }

    template<class Fn, size_t N>
    Result<N> optimize(NLopt &nl, Fn &&fn, const Input<N> &initvals)
    {
        Result<N> r;

        TOptData<Fn> data = std::make_tuple(&fn, this, nl.ptr);

        switch(m_dir) {
        case OptDir::MIN:
            nlopt_set_min_objective(nl.ptr, optfunc<Fn, N>, &data); break;
        case OptDir::MAX:
            nlopt_set_max_objective(nl.ptr, optfunc<Fn, N>, &data); break;
        }

        r.optimum = initvals;
        r.resultcode = nlopt_optimize(nl.ptr, r.optimum.data(), &r.score);

        return r;
    }

public:

    template<class Func, size_t N>
    Result<N> optimize(Func&& func,
                       const Input<N> &initvals,
                       const Bounds<N>& bounds)
    {
        NLopt nl{alg, N};
        set_up(nl, bounds);

        return optimize(nl, std::forward<Func>(func), initvals);
    }

    explicit NLoptOpt(StopCriteria stopcr = {}) : m_stopcr(stopcr) {}

    void set_criteria(const StopCriteria &cr) { m_stopcr = cr; }
    const StopCriteria &get_criteria() const noexcept { return m_stopcr; }
    void set_dir(OptDir dir) noexcept { m_dir = dir; }

    void seed(long s) { nlopt_srand(s); }
};

template<nlopt_algorithm glob, nlopt_algorithm loc>
class NLoptOpt<NLoptAlgComb<glob, loc>>: public NLoptOpt<NLoptAlg<glob>>
{
    using Base = NLoptOpt<NLoptAlg<glob>>;
public:

    template<class Fn, size_t N>
    Result<N> optimize(Fn&& f,
                       const Input<N> &initvals,
                       const Bounds<N>& bounds)
    {
        NLopt nl_glob{glob, N}, nl_loc{loc, N};

        Base::set_up(nl_glob, bounds);
        Base::set_up(nl_loc, bounds);
        nlopt_set_local_optimizer(nl_glob.ptr, nl_loc.ptr);

        return Base::optimize(nl_glob, std::forward<Fn>(f), initvals);
    }

    explicit NLoptOpt(StopCriteria stopcr = {}) : Base{stopcr} {}
};

} // namespace detail;

// Optimizers based on NLopt.
template<class M> class Optimizer<M, detail::NLoptOnly<M>> {
    detail::NLoptOpt<M> m_opt;

public:

    Optimizer& to_max() { m_opt.set_dir(detail::OptDir::MAX); return *this; }
    Optimizer& to_min() { m_opt.set_dir(detail::OptDir::MIN); return *this; }

    template<class Func, size_t N>
    Result<N> optimize(Func&& func,
                       const Input<N> &initvals,
                       const Bounds<N>& bounds)
    {
        return m_opt.optimize(std::forward<Func>(func), initvals, bounds);
    }

    explicit Optimizer(StopCriteria stopcr = {}) : m_opt(stopcr) {}

    Optimizer &set_criteria(const StopCriteria &cr)
    {
        m_opt.set_criteria(cr); return *this;
    }

    const StopCriteria &get_criteria() const { return m_opt.get_criteria(); }

    void seed(long s) { m_opt.seed(s); }
};

template<size_t N> Bounds<N> bounds(const Bound (&b) [N]) { return detail::to_arr(b); }
template<size_t N> Input<N> initvals(const double (&a) [N]) { return detail::to_arr(a); }
template<size_t N> auto score_gradient(double s, const double (&grad)[N])
{
    return ScoreGradient<N>(s, detail::to_arr(grad));
}

// Predefinded NLopt algorithms that are used in the codebase
using AlgNLoptGenetic = detail::NLoptAlgComb<NLOPT_GN_ESCH>;
using AlgNLoptSubplex = detail::NLoptAlg<NLOPT_LN_SBPLX>;
using AlgNLoptSimplex = detail::NLoptAlg<NLOPT_LN_NELDERMEAD>;
using AlgNLoptDIRECT  = detail::NLoptAlg<NLOPT_GN_DIRECT>;

// TODO: define others if needed...

// Helper defs for pre-crafted global and local optimizers that work well.
using DefaultGlobalOptimizer = Optimizer<AlgNLoptGenetic>;
using DefaultLocalOptimizer  = Optimizer<AlgNLoptSubplex>;

}} // namespace Slic3r::opt

#endif // NLOPTOPTIMIZER_HPP
