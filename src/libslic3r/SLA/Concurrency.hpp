#ifndef SLA_CONCURRENCY_H
#define SLA_CONCURRENCY_H

#include <tbb/spin_mutex.h>
#include <tbb/mutex.h>
#include <tbb/parallel_for.h>

namespace Slic3r {
namespace sla {

// Set this to true to enable full parallelism in this module.
// Only the well tested parts will be concurrent if this is set to false.
const constexpr bool USE_FULL_CONCURRENCY = true;

template<bool> struct _ccr {};

template<> struct _ccr<true>
{
    using SpinningMutex = tbb::spin_mutex;
    using BlockingMutex  = tbb::mutex;
    
    template<class It, class Fn>
    static inline void enumerate(It from, It to, Fn fn)
    {
        auto   iN = to - from;
        size_t N  = iN < 0 ? 0 : size_t(iN);
        
        tbb::parallel_for(size_t(0), N, [from, fn](size_t n) {
            fn(*(from + decltype(iN)(n)), n);
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
    static inline void enumerate(It from, It to, Fn fn)
    {
        for (auto it = from; it != to; ++it) fn(*it, size_t(it - from));
    }
};

using ccr = _ccr<USE_FULL_CONCURRENCY>;
using ccr_seq = _ccr<false>;
using ccr_par = _ccr<true>;

}} // namespace Slic3r::sla

#endif // SLACONCURRENCY_H
