#ifndef SLA_CONCURRENCY_H
#define SLA_CONCURRENCY_H

#include <tbb/spin_mutex.h>
#include <tbb/mutex.h>
#include <tbb/parallel_for.h>
#include <algorithm>
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

    template<class It, class Fn>
    static IteratorOnly<It, void> for_each(It     from,
                                           It     to,
                                           Fn &&  fn,
                                           size_t granularity = 1)
    {
        tbb::parallel_for(tbb::blocked_range{from, to, granularity},
                          [&fn, from](const auto &range) {
                              for (auto &el : range) fn(el);
                          });
    }

    template<class I, class Fn>
    static IntegerOnly<I, void> for_each(I      from,
                                         I      to,
                                         Fn &&  fn,
                                         size_t granularity = 1)
    {
        tbb::parallel_for(tbb::blocked_range{from, to, granularity},
                          [&fn](const auto &range) {
            for (I i = range.begin(); i < range.end(); ++i) fn(i);
        });
    }
};

template<> struct _ccr<false>
{
private:
    struct _Mtx { inline void lock() {} inline void unlock() {} };
    
public:
    using SpinningMutex = _Mtx;
    using BlockingMutex = _Mtx;

    template<class It, class Fn>
    static IteratorOnly<It, void> for_each(It   from,
                                           It   to,
                                           Fn &&fn,
                                           size_t /* ignore granularity */ = 1)
    {
        for (auto it = from; it != to; ++it) fn(*it);
    }

    template<class I, class Fn>
    static IntegerOnly<I, void> for_each(I    from,
                                         I    to,
                                         Fn &&fn,
                                         size_t /* ignore granularity */ = 1)
    {
        for (I i = from; i < to; ++i) fn(i);
    }
};

using ccr = _ccr<USE_FULL_CONCURRENCY>;
using ccr_seq = _ccr<false>;
using ccr_par = _ccr<true>;

}} // namespace Slic3r::sla

#endif // SLACONCURRENCY_H
