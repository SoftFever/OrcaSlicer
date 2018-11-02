#ifndef SPATINDEX_HPP
#define SPATINDEX_HPP

#include <memory>
#include <utility>
#include <vector>

#include <Eigen/Geometry>

namespace Slic3r {
namespace sla {

typedef Eigen::Matrix<double,   3, 1, Eigen::DontAlign> Vec3d;
using SpatElement = std::pair<Vec3d, unsigned>;

/**
 * This class is intended for enhancing range based for loops with indexing.
 * So instead of this:
 * { int i = 0; for(auto c : container) { process(c, i); ++i; }
 *
 * you can use this:
 * for(auto ic : container) process(ic.value, ic.index);
 */
template<class Container> class Enumerable {
    Container&& m;
    using C = typename std::remove_reference<Container>::type;
    using CC = typename std::remove_const<C>::type;

    template<class S> struct get_iterator {};
    template<> struct get_iterator<CC> { using type = typename CC::iterator; };
    template<> struct get_iterator<const CC> {
        using type = typename CC::const_iterator;
    };

    template<class Vref> struct _Pair {
        Vref value;
        size_t index;
        _Pair(Vref v, size_t i) : value(v), index(i) {}
        operator Vref() { return value; }
    };

    template<class Cit>
    class _iterator {
        Cit start;
        Cit it;
        using Pair = _Pair<typename std::iterator_traits<Cit>::reference>;
    public:
        _iterator(Cit b, Cit i): start(b), it(i) {}
        _iterator& operator++() { ++it; return *this;}
        _iterator operator++(int) { auto tmp = it; ++it; return tmp;}

        bool operator!=(_iterator other) { return it != other.it; }
        Pair operator*() { return Pair(*it, it - start); }
        using value_type = typename Enumerable::value_type;
    };

public:

    Enumerable(Container&& c): m(c) {}

    using value_type = typename CC::value_type;

    using iterator = _iterator<typename get_iterator<C>::type>;
    using const_iterator = _iterator<typename CC::const_iterator>;

    iterator begin() { return iterator(m.begin(), m.begin()); }
    iterator end()   { return iterator(m.begin(), m.end()); }
    const_iterator begin() const {return const_iterator(m.cbegin(), m.cbegin());}
    const_iterator end() const { return const_iterator(m.cbegin(), m.cend());}

};

template<class C> inline Enumerable<C> enumerate(C&& c) {
    return Enumerable<C>(std::forward<C>(c));
}

class SpatIndex {
    class Impl;

    // We use Pimpl because it takes a long time to compile boost headers which
    // is the engine of this class. We include it only in the cpp file.
    std::unique_ptr<Impl> m_impl;
public:

    SpatIndex();
    ~SpatIndex();

    SpatIndex(const SpatIndex&);
    SpatIndex(SpatIndex&&);
    SpatIndex& operator=(const SpatIndex&);
    SpatIndex& operator=(SpatIndex&&);

    void insert(const SpatElement&);
    bool remove(const SpatElement&);

    inline void insert(const Vec3d& v, unsigned idx)
    {
        insert(std::make_pair(v, unsigned(idx)));
    }

    std::vector<SpatElement> query(std::function<bool(const SpatElement&)>);
    std::vector<SpatElement> nearest(const Vec3d&, unsigned k);

    // For testing
    size_t size() const;
    bool empty() const { return size() == 0; }
};

}
}

#endif // SPATINDEX_HPP
