#ifndef MTUTILS_HPP
#define MTUTILS_HPP

#include <atomic>       // for std::atomic_flag
#include <mutex>        // for std::lock_guard
#include <functional>   // for std::function
#include <utility>      // for std::forward

namespace Slic3r {

// TODO: these classes are untested

/// Handy little spin mutex for the cached meshes.
/// Implements the "Lockable" concept
class SpinMutex {
    std::atomic_flag m_flg;
    static const /*constexpr*/ auto MO_ACQ = std::memory_order_acquire;
    static const /*constexpr*/ auto MO_REL = std::memory_order_release;
public:
    inline SpinMutex() { m_flg.clear(MO_REL); }
    inline void lock() { while(m_flg.test_and_set(MO_ACQ)); }
    inline bool try_lock() { return !m_flg.test_and_set(MO_ACQ); }
    inline void unlock() { m_flg.clear(MO_REL); }
};

/// A wrapper class around arbitrary object that needs thread safe caching.
template<class T> class CachedObject {
public:
    // Method type which refreshes the object when it has been invalidated
    using Setter = std::function<void(T&)>;
private:
    T m_obj;            // the object itself
    bool m_valid;       // invalidation flag
    SpinMutex m_lck;    // to make the caching thread safe

    // the setter will be called just before the object's const value is about
    // to be retrieved.
    std::function<void(T&)> m_setter;
public:

    // Forwarded constructor
    template<class...Args> inline CachedObject(Setter fn, Args&&...args):
        m_obj(std::forward<Args>(args)...), m_valid(false), m_setter(fn) {}

    // invalidate the value of the object. The object will be refreshed at the
    // next retrieval (Setter will be called). The data that is used in
    // the setter function should be guarded as well if it is modified so the
    // modification has to take place in fn.
    inline void invalidate(std::function<void()> fn) {
        std::lock_guard<SpinMutex> lck(m_lck); fn(); m_valid = false;
    }

    // Get the const object properly updated.
    inline const T& get() {
        std::lock_guard<SpinMutex> lck(m_lck);
        if(!m_valid) { m_setter(m_obj); m_valid = true; }
        return m_obj;
    }
};

}

#endif // MTUTILS_HPP
