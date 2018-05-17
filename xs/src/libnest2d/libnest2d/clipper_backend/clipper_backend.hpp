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

// Tell binpack2d how to make string out of a ClipperPolygon object
template<>
inline double ShapeLike::area(const PolygonImpl& sh) {
    #define DISABLE_BOOST_AREA
    double ret = ClipperLib::Area(sh.Contour);
//    if(OrientationType<PolygonImpl>::Value == Orientation::COUNTER_CLOCKWISE)
//        ret = -ret;
    return ret;
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

template<>
PolygonImpl ShapeLike::create( std::initializer_list< PointImpl > il);

template<>
const THolesContainer<PolygonImpl>& ShapeLike::holes(
        const PolygonImpl& sh);

template<>
THolesContainer<PolygonImpl>& ShapeLike::holes(PolygonImpl& sh);

template<>
inline TCountour<PolygonImpl>& ShapeLike::getHole(PolygonImpl& sh,
                                                  unsigned long idx)
{
    return sh.Childs[idx]->Contour;
}

template<>
inline const TCountour<PolygonImpl>& ShapeLike::getHole(const PolygonImpl& sh,
                                                        unsigned long idx) {
    return sh.Childs[idx]->Contour;
}

template<>
inline size_t ShapeLike::holeCount(const PolygonImpl& sh) {
    return sh.Childs.size();
}

template<>
inline PathImpl& ShapeLike::getContour(PolygonImpl& sh) {
    return sh.Contour;
}

template<>
inline const PathImpl& ShapeLike::getContour(const PolygonImpl& sh) {
    return sh.Contour;
}

}

//#define DISABLE_BOOST_SERIALIZE
//#define DISABLE_BOOST_UNSERIALIZE

// All other operators and algorithms are implemented with boost
#include "../boost_alg.hpp"

#endif // CLIPPER_BACKEND_HPP
