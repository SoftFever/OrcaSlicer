#ifndef MTUTILS_HPP
#define MTUTILS_HPP

#include <atomic>     // for std::atomic_flag and memory orders
#include <mutex>      // for std::lock_guard
#include <functional> // for std::function
#include <utility>    // for std::forward
#include <algorithm>

#include "libslic3r.h"
#include "Point.hpp"

namespace Slic3r {

/// Handy little spin mutex for the cached meshes.
/// Implements the "Lockable" concept
class SpinMutex
{
    std::atomic_flag                m_flg;
    static const /*constexpr*/ auto MO_ACQ = std::memory_order_acquire;
    static const /*constexpr*/ auto MO_REL = std::memory_order_release;

public:
    inline SpinMutex() { m_flg.clear(MO_REL); }
    inline void lock() { while (m_flg.test_and_set(MO_ACQ)) ; }
    inline bool try_lock() { return !m_flg.test_and_set(MO_ACQ); }
    inline void unlock() { m_flg.clear(MO_REL); }
};

/// A wrapper class around arbitrary object that needs thread safe caching.
template<class T> class CachedObject
{
public:
    // Method type which refreshes the object when it has been invalidated
    using Setter = std::function<void(T &)>;

private:
    T         m_obj;   // the object itself
    bool      m_valid; // invalidation flag
    SpinMutex m_lck;   // to make the caching thread safe

    // the setter will be called just before the object's const value is
    // about to be retrieved.
    std::function<void(T &)> m_setter;

public:
    // Forwarded constructor
    template<class... Args>
    inline CachedObject(Setter fn, Args &&... args)
        : m_obj(std::forward<Args>(args)...), m_valid(false), m_setter(fn)
    {}

    // invalidate the value of the object. The object will be refreshed at
    // the next retrieval (Setter will be called). The data that is used in
    // the setter function should be guarded as well during modification so
    // the modification has to take place in fn.
    inline void invalidate(std::function<void()> fn)
    {
        std::lock_guard<SpinMutex> lck(m_lck);
        fn();
        m_valid = false;
    }

    // Get the const object properly updated.
    inline const T &get()
    {
        std::lock_guard<SpinMutex> lck(m_lck);
        if (!m_valid) {
            m_setter(m_obj);
            m_valid = true;
        }
        return m_obj;
    }
};

/// An std compatible random access iterator which uses indices to the
/// source vector thus resistant to invalidation caused by relocations. It
/// also "knows" its container. No comparison is neccesary to the container
/// "end()" iterator. The template can be instantiated with a different
/// value type than that of the container's but the types must be
/// compatible. E.g. a base class of the contained objects is compatible.
///
/// For a constant iterator, one can instantiate this template with a value
/// type preceded with 'const'.
template<class Vector, // The container type, must be random access...
         class Value = typename Vector::value_type // The value type
         >
class IndexBasedIterator
{
    static const size_t NONE = size_t(-1);

    std::reference_wrapper<Vector> m_index_ref;
    size_t                         m_idx = NONE;

public:
    using value_type        = Value;
    using pointer           = Value *;
    using reference         = Value &;
    using difference_type   = long;
    using iterator_category = std::random_access_iterator_tag;

    inline explicit IndexBasedIterator(Vector &index, size_t idx)
        : m_index_ref(index), m_idx(idx)
    {}

    // Post increment
    inline IndexBasedIterator operator++(int)
    {
        IndexBasedIterator cpy(*this);
        ++m_idx;
        return cpy;
    }

    inline IndexBasedIterator operator--(int)
    {
        IndexBasedIterator cpy(*this);
        --m_idx;
        return cpy;
    }

    inline IndexBasedIterator &operator++()
    {
        ++m_idx;
        return *this;
    }

    inline IndexBasedIterator &operator--()
    {
        --m_idx;
        return *this;
    }

    inline IndexBasedIterator &operator+=(difference_type l)
    {
        m_idx += size_t(l);
        return *this;
    }

    inline IndexBasedIterator operator+(difference_type l)
    {
        auto cpy = *this;
        cpy += l;
        return cpy;
    }

    inline IndexBasedIterator &operator-=(difference_type l)
    {
        m_idx -= size_t(l);
        return *this;
    }

    inline IndexBasedIterator operator-(difference_type l)
    {
        auto cpy = *this;
        cpy -= l;
        return cpy;
    }

    operator difference_type() { return difference_type(m_idx); }

    /// Tesing the end of the container... this is not possible with std
    /// iterators.
    inline bool is_end() const
    {
        return m_idx >= m_index_ref.get().size();
    }

    inline Value &operator*() const
    {
        assert(m_idx < m_index_ref.get().size());
        return m_index_ref.get().operator[](m_idx);
    }

    inline Value *operator->() const
    {
        assert(m_idx < m_index_ref.get().size());
        return &m_index_ref.get().operator[](m_idx);
    }

    /// If both iterators point past the container, they are equal...
    inline bool operator==(const IndexBasedIterator &other)
    {
        size_t e = m_index_ref.get().size();
        return m_idx == other.m_idx || (m_idx >= e && other.m_idx >= e);
    }

    inline bool operator!=(const IndexBasedIterator &other)
    {
        return !(*this == other);
    }

    inline bool operator<=(const IndexBasedIterator &other)
    {
        return (m_idx < other.m_idx) || (*this == other);
    }

    inline bool operator<(const IndexBasedIterator &other)
    {
        return m_idx < other.m_idx && (*this != other);
    }

    inline bool operator>=(const IndexBasedIterator &other)
    {
        return m_idx > other.m_idx || *this == other;
    }

    inline bool operator>(const IndexBasedIterator &other)
    {
        return m_idx > other.m_idx && *this != other;
    }
};

/// A very simple range concept implementation with iterator-like objects.
template<class It> class Range
{
    It from, to;

public:
    // The class is ready for range based for loops.
    It begin() const { return from; }
    It end() const { return to; }

    // The iterator type can be obtained this way.
    using Type = It;

    Range() = default;
    Range(It &&b, It &&e)
        : from(std::forward<It>(b)), to(std::forward<It>(e))
    {}

    // Some useful container-like methods...
    inline size_t size() const { return end() - begin(); }
    inline bool   empty() const { return size() == 0; }
};

template<class C> bool all_of(const C &container)
{
    return std::all_of(container.begin(),
                       container.end(),
                       [](const typename C::value_type &v) {
                           return static_cast<bool>(v);
                       });
}

// A shorter C++14 style form of the enable_if metafunction
template<bool B, class T>
using enable_if_t = typename std::enable_if<B, T>::type;

// /////////////////////////////////////////////////////////////////////////////
// Type safe conversions to and from scaled and unscaled coordinates
// /////////////////////////////////////////////////////////////////////////////

// A meta-predicate which is true for integers wider than or equal to coord_t
template<class I> struct is_scaled_coord
{
    static const SLIC3R_CONSTEXPR bool value =
        std::is_integral<I>::value &&
        std::numeric_limits<I>::digits >=
            std::numeric_limits<coord_t>::digits;
};

// Meta predicates for floating, 'scaled coord' and generic arithmetic types
template<class T>
using FloatingOnly = enable_if_t<std::is_floating_point<T>::value, T>;

template<class T>
using ScaledCoordOnly = enable_if_t<is_scaled_coord<T>::value, T>;

template<class T>
using ArithmeticOnly = enable_if_t<std::is_arithmetic<T>::value, T>;

// A shorter form for a generic Eigen vector which is widely used in PrusaSlicer 
template<class T, int N>
using EigenVec = Eigen::Matrix<T, N, 1, Eigen::DontAlign>;

// Semantics are the following:
// Upscaling (scaled()): only from floating point types (or Vec) to either
//                       floating point or integer 'scaled coord' coordinates.
// Downscaling (unscaled()): from arithmetic types (or Vec) to either
//                           floating point only

// Conversion definition from unscaled to floating point scaled
template<class Tout,
         class Tin,
         class = FloatingOnly<Tin>,
         class = FloatingOnly<Tout>>
inline SLIC3R_CONSTEXPR Tout scaled(const Tin &v) SLIC3R_NOEXCEPT
{
    return static_cast<Tout>(v / static_cast<Tin>(SCALING_FACTOR));
}

// Conversion definition from unscaled to integer 'scaled coord'.
// TODO: is the rounding necessary ? Here it is to show that it can be different
// but it does not have to be. Using std::round means loosing noexcept and
// constexpr modifiers
template<class Tout = coord_t, class Tin, class = FloatingOnly<Tin>>
inline SLIC3R_CONSTEXPR ScaledCoordOnly<Tout> scaled(const Tin &v) SLIC3R_NOEXCEPT
{
    //return static_cast<Tout>(std::round(v / SCALING_FACTOR));
    return static_cast<Tout>(v / static_cast<Tin>(SCALING_FACTOR));
}

// Conversion for Eigen vectors (N dimensional points)
template<class Tout = coord_t, class Tin, int N, class = FloatingOnly<Tin>>
inline EigenVec<ArithmeticOnly<Tout>, N> scaled(const EigenVec<Tin, N> &v)
{
    return v.template cast<Tout>() / SCALING_FACTOR;
}

// Conversion from arithmetic scaled type to floating point unscaled
template<class Tout = double,
         class Tin,
         class = ArithmeticOnly<Tin>,
         class = FloatingOnly<Tout>>
inline SLIC3R_CONSTEXPR Tout unscaled(const Tin &v) SLIC3R_NOEXCEPT
{
    return static_cast<Tout>(v * static_cast<Tout>(SCALING_FACTOR));
}

// Unscaling for Eigen vectors. Input base type can be arithmetic, output base
// type can only be floating point.
template<class Tout = double,
         class Tin,
         int N,
         class = ArithmeticOnly<Tin>,
         class = FloatingOnly<Tout>>
inline SLIC3R_CONSTEXPR EigenVec<Tout, N> unscaled(
    const EigenVec<Tin, N> &v) SLIC3R_NOEXCEPT
{
    return v.template cast<Tout>() * SCALING_FACTOR;
}

} // namespace Slic3r

#endif // MTUTILS_HPP
