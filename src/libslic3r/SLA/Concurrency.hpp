#ifndef SLA_CONCURRENCY_H
#define SLA_CONCURRENCY_H

#include <tbb/spin_mutex.h>
#include <tbb/mutex.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

#include <algorithm>
#include <numeric>

#include <libslic3r/libslic3r.h>

namespace Slic3r {
namespace sla {

// Set this to true to enable full parallelism in this module.
// Only the well tested parts will be concurrent if this is set to false.
const constexpr bool USE_FULL_CONCURRENCY = true;

template<bool> struct _ccr {};

template<> struct _ccr<true>
{
    using SpinningMutex = tbb::spin_mutex;
    using BlockingMutex = tbb::mutex;

    template<class Fn, class It>
    static IteratorOnly<It, void> loop_(const tbb::blocked_range<It> &range, Fn &&fn)
    {
        for (auto &el : range) fn(el);
    }

    template<class Fn, class I>
    static IntegerOnly<I, void> loop_(const tbb::blocked_range<I> &range, Fn &&fn)
    {
        for (I i = range.begin(); i < range.end(); ++i) fn(i);
    }

    template<class It, class Fn>
    static void for_each(It from, It to, Fn &&fn, size_t granularity = 1)
    {
        tbb::parallel_for(tbb::blocked_range{from, to, granularity},
                          [&fn, from](const auto &range) {
            loop_(range, std::forward<Fn>(fn));
        });
    }

    template<class I, class MergeFn, class T, class AccessFn>
    static T reduce(I          from,
                    I          to,
                    const T   &init,
                    MergeFn  &&mergefn,
                    AccessFn &&access,
                    size_t     granularity = 1
                    )
    {
        return tbb::parallel_reduce(
            tbb::blocked_range{from, to, granularity}, init,
            [&](const auto &range, T subinit) {
                T acc = subinit;
                loop_(range, [&](auto &i) { acc = mergefn(acc, access(i)); });
                return acc;
            },
            std::forward<MergeFn>(mergefn));
    }

    template<class I, class MergeFn, class T>
    static IteratorOnly<I, T> reduce(I         from,
                                     I         to,
                                     const T & init,
                                     MergeFn &&mergefn,
                                     size_t    granularity = 1)
    {
        return reduce(
            from, to, init, std::forward<MergeFn>(mergefn),
            [](typename I::value_type &i) { return i; }, granularity);
    }
};

template<> struct _ccr<false>
{
private:
    struct _Mtx { inline void lock() {} inline void unlock() {} };
    
public:
    using SpinningMutex = _Mtx;
    using BlockingMutex = _Mtx;

    template<class Fn, class It>
    static IteratorOnly<It, void> loop_(It from, It to, Fn &&fn)
    {
        for (auto it = from; it != to; ++it) fn(*it);
    }

    template<class Fn, class I>
    static IntegerOnly<I, void> loop_(I from, I to, Fn &&fn)
    {
        for (I i = from; i < to; ++i) fn(i);
    }

    template<class It, class Fn>
    static void for_each(It   from,
                         It   to,
                         Fn &&fn,
                         size_t /* ignore granularity */ = 1)
    {
        loop_(from, to, std::forward<Fn>(fn));
    }

    template<class I, class MergeFn, class T, class AccessFn>
    static T reduce(I         from,
                    I         to,
                    const T & init,
                    MergeFn  &&mergefn,
                    AccessFn &&access,
                    size_t   /*granularity*/ = 1
                    )
    {
        T acc = init;
        loop_(from, to, [&](auto &i) { acc = mergefn(acc, access(i)); });
        return acc;
    }

    template<class I, class MergeFn, class T>
    static IteratorOnly<I, T> reduce(I          from,
                                     I          to,
                                     const T   &init,
                                     MergeFn  &&mergefn,
                                     size_t     /*granularity*/ = 1
                                     )
    {
        return reduce(from, to, init, std::forward<MergeFn>(mergefn),
                      [](typename I::value_type &i) { return i; });
    }
};

using ccr = _ccr<USE_FULL_CONCURRENCY>;
using ccr_seq = _ccr<false>;
using ccr_par = _ccr<true>;

}} // namespace Slic3r::sla

#endif // SLACONCURRENCY_H
