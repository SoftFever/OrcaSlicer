#ifndef CLIPPER_BACKEND_HPP
#define CLIPPER_BACKEND_HPP

#include <sstream>
#include <unordered_map>
#include <cassert>

#include <iostream>

#include "../geometry_traits.hpp"
#include "../geometries_nfp.hpp"

#include <clipper.hpp>

namespace libnest2d {

// Aliases for convinience
using PointImpl = ClipperLib::IntPoint;
using PolygonImpl = ClipperLib::PolyNode;
using PathImpl = ClipperLib::Path;

inline PointImpl& operator +=(PointImpl& p, const PointImpl& pa ) {
    // This could be done with SIMD
    p.X += pa.X;
    p.Y += pa.Y;
    return p;
}

inline PointImpl operator+(const PointImpl& p1, const PointImpl& p2) {
    PointImpl ret = p1;
    ret += p2;
    return ret;
}

inline PointImpl& operator -=(PointImpl& p, const PointImpl& pa ) {
    p.X -= pa.X;
    p.Y -= pa.Y;
    return p;
}

inline PointImpl operator -(PointImpl& p ) {
    PointImpl ret = p;
    ret.X = -ret.X;
    ret.Y = -ret.Y;
    return ret;
}

inline PointImpl operator-(const PointImpl& p1, const PointImpl& p2) {
    PointImpl ret = p1;
    ret -= p2;
    return ret;
}

//extern HoleCache holeCache;

// Type of coordinate units used by Clipper
template<> struct CoordType<PointImpl> {
    using Type = ClipperLib::cInt;
};

// Type of point used by Clipper
template<> struct PointType<PolygonImpl> {
    using Type = PointImpl;
};

// Type of vertex iterator used by Clipper
template<> struct VertexIteratorType<PolygonImpl> {
    using Type = ClipperLib::Path::iterator;
};

// Type of vertex iterator used by Clipper
template<> struct VertexConstIteratorType<PolygonImpl> {
    using Type = ClipperLib::Path::const_iterator;
};

template<> struct CountourType<PolygonImpl> {
    using Type = PathImpl;
};

// Tell binpack2d how to extract the X coord from a ClipperPoint object
template<> inline TCoord<PointImpl> PointLike::x(const PointImpl& p)
{
    return p.X;
}

// Tell binpack2d how to extract the Y coord from a ClipperPoint object
template<> inline TCoord<PointImpl> PointLike::y(const PointImpl& p)
{
    return p.Y;
}

// Tell binpack2d how to extract the X coord from a ClipperPoint object
template<> inline TCoord<PointImpl>& PointLike::x(PointImpl& p)
{
    return p.X;
}

// Tell binpack2d how to extract the Y coord from a ClipperPoint object
template<>
inline TCoord<PointImpl>& PointLike::y(PointImpl& p)
{
    return p.Y;
}

template<>
inline void ShapeLike::reserve(PolygonImpl& sh, unsigned long vertex_capacity)
{
    return sh.Contour.reserve(vertex_capacity);
}

#define DISABLE_BOOST_AREA

namespace _smartarea {
template<Orientation o>
inline double area(const PolygonImpl& sh) {
    return std::nan("");
}

template<>
inline double area<Orientation::CLOCKWISE>(const PolygonImpl& sh) {
    return -ClipperLib::Area(sh.Contour);
}

template<>
inline double area<Orientation::COUNTER_CLOCKWISE>(const PolygonImpl& sh) {
    return ClipperLib::Area(sh.Contour);
}
}

// Tell binpack2d how to make string out of a ClipperPolygon object
template<>
inline double ShapeLike::area(const PolygonImpl& sh) {
    return _smartarea::area<OrientationType<PolygonImpl>::Value>(sh);
}

template<>
inline void ShapeLike::offset(PolygonImpl& sh, TCoord<PointImpl> distance) {
    #define DISABLE_BOOST_OFFSET

    using ClipperLib::ClipperOffset;
    using ClipperLib::jtMiter;
    using ClipperLib::etClosedPolygon;
    using ClipperLib::Paths;

    ClipperOffset offs;
    Paths result;
    offs.AddPath(sh.Contour, jtMiter, etClosedPolygon);
    offs.Execute(result, static_cast<double>(distance));

    // I dont know why does the offsetting revert the orientation and
    // it removes the last vertex as well so boost will not have a closed
    // polygon

    assert(result.size() == 1);
    sh.Contour = result.front();

    // recreate closed polygon
    sh.Contour.push_back(sh.Contour.front());

    if(ClipperLib::Orientation(sh.Contour)) {
        // Not clockwise then reverse the b*tch
        ClipperLib::ReversePath(sh.Contour);
    }

}

template<>
inline PolygonImpl& Nfp::minkowskiAdd(PolygonImpl& sh,
                                            const PolygonImpl& other)
{
    #define DISABLE_BOOST_MINKOWSKI_ADD

    ClipperLib::Paths solution;

    ClipperLib::MinkowskiSum(sh.Contour, other.Contour, solution, true);

    assert(solution.size() == 1);

    sh.Contour = solution.front();

    return sh;
}

// Tell binpack2d how to make string out of a ClipperPolygon object
template<> std::string ShapeLike::toString(const PolygonImpl& sh);

template<>
inline TVertexIterator<PolygonImpl> ShapeLike::begin(PolygonImpl& sh)
{
    return sh.Contour.begin();
}

template<>
inline TVertexIterator<PolygonImpl> ShapeLike::end(PolygonImpl& sh)
{
    return sh.Contour.end();
}

template<>
inline TVertexConstIterator<PolygonImpl> ShapeLike::cbegin(
        const PolygonImpl& sh)
{
    return sh.Contour.cbegin();
}

template<>
inline TVertexConstIterator<PolygonImpl> ShapeLike::cend(
        const PolygonImpl& sh)
{
    return sh.Contour.cend();
}

template<> struct HolesContainer<PolygonImpl> {
    using Type = ClipperLib::Paths;
};

template<> PolygonImpl ShapeLike::create( const PathImpl& path);

template<> PolygonImpl ShapeLike::create( PathImpl&& path);

template<>
const THolesContainer<PolygonImpl>& ShapeLike::holes(
        const PolygonImpl& sh);

template<>
THolesContainer<PolygonImpl>& ShapeLike::holes(PolygonImpl& sh);

template<>
inline TContour<PolygonImpl>& ShapeLike::getHole(PolygonImpl& sh,
                                                  unsigned long idx)
{
    return sh.Childs[idx]->Contour;
}

template<>
inline const TContour<PolygonImpl>& ShapeLike::getHole(const PolygonImpl& sh,
                                                        unsigned long idx)
{
    return sh.Childs[idx]->Contour;
}

template<> inline size_t ShapeLike::holeCount(const PolygonImpl& sh)
{
    return sh.Childs.size();
}

template<> inline PathImpl& ShapeLike::getContour(PolygonImpl& sh)
{
    return sh.Contour;
}

template<>
inline const PathImpl& ShapeLike::getContour(const PolygonImpl& sh)
{
    return sh.Contour;
}

#define DISABLE_BOOST_TRANSLATE
template<>
inline void ShapeLike::translate(PolygonImpl& sh, const PointImpl& offs)
{
    for(auto& p : sh.Contour) { p += offs; }
    for(auto& hole : sh.Childs) for(auto& p : hole->Contour) { p += offs; }
}

#define DISABLE_BOOST_NFP_MERGE

template<> inline
Nfp::Shapes<PolygonImpl> Nfp::merge(const Nfp::Shapes<PolygonImpl>& shapes,
                                    const PolygonImpl& sh)
{
    Nfp::Shapes<PolygonImpl> retv;

    ClipperLib::Clipper clipper;

    bool closed =  true;

#ifndef NDEBUG
#define _valid() valid =
    bool valid = false;
#else
#define _valid()
#endif

    _valid() clipper.AddPath(sh.Contour, ClipperLib::ptSubject, closed);

    for(auto& hole : sh.Childs) {
        _valid() clipper.AddPath(hole->Contour, ClipperLib::ptSubject, closed);
        assert(valid);
    }

    for(auto& path : shapes) {
        _valid() clipper.AddPath(path.Contour, ClipperLib::ptSubject, closed);
        assert(valid);
        for(auto& hole : path.Childs) {
            _valid() clipper.AddPath(hole->Contour, ClipperLib::ptSubject, closed);
            assert(valid);
        }
    }

    ClipperLib::Paths rret;
    clipper.Execute(ClipperLib::ctUnion, rret, ClipperLib::pftNonZero);
    retv.reserve(rret.size());
    for(auto& p : rret) {
        if(ClipperLib::Orientation(p)) {
            // Not clockwise then reverse the b*tch
            ClipperLib::ReversePath(p);
        }
        retv.emplace_back();
        retv.back().Contour = p;
        retv.back().Contour.emplace_back(p.front());
    }

    return retv;
}

}

//#define DISABLE_BOOST_SERIALIZE
//#define DISABLE_BOOST_UNSERIALIZE

// All other operators and algorithms are implemented with boost
#include "../boost_alg.hpp"

#endif // CLIPPER_BACKEND_HPP
