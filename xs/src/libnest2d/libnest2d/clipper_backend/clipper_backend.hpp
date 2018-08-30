#ifndef CLIPPER_BACKEND_HPP
#define CLIPPER_BACKEND_HPP

#include <sstream>
#include <unordered_map>
#include <cassert>
#include <vector>
#include <iostream>

#include "../geometry_traits.hpp"
#include "../geometry_traits_nfp.hpp"

#include <clipper.hpp>

namespace ClipperLib {
using PointImpl = IntPoint;
using PathImpl = Path;
using HoleStore = std::vector<PathImpl>;

struct PolygonImpl {
    PathImpl Contour;
    HoleStore Holes;

    inline PolygonImpl() = default;

    inline explicit PolygonImpl(const PathImpl& cont): Contour(cont) {}
    inline explicit PolygonImpl(const HoleStore& holes):
        Holes(holes) {}
    inline PolygonImpl(const Path& cont, const HoleStore& holes):
        Contour(cont), Holes(holes) {}

    inline explicit PolygonImpl(PathImpl&& cont): Contour(std::move(cont)) {}
    inline explicit PolygonImpl(HoleStore&& holes): Holes(std::move(holes)) {}
    inline PolygonImpl(Path&& cont, HoleStore&& holes):
        Contour(std::move(cont)), Holes(std::move(holes)) {}
};

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

inline PointImpl& operator *=(PointImpl& p, const PointImpl& pa ) {
    p.X *= pa.X;
    p.Y *= pa.Y;
    return p;
}

inline PointImpl operator*(const PointImpl& p1, const PointImpl& p2) {
    PointImpl ret = p1;
    ret *= p2;
    return ret;
}

}

namespace libnest2d {

// Aliases for convinience
using ClipperLib::PointImpl;
using ClipperLib::PathImpl;
using ClipperLib::PolygonImpl;
using ClipperLib::HoleStore;

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
inline void ShapeLike::reserve(PolygonImpl& sh, size_t vertex_capacity)
{
    return sh.Contour.reserve(vertex_capacity);
}

#define DISABLE_BOOST_AREA

namespace _smartarea {
template<Orientation o>
inline double area(const PolygonImpl& /*sh*/) {
    return std::nan("");
}

template<>
inline double area<Orientation::CLOCKWISE>(const PolygonImpl& sh) {
    double a = 0;

    std::for_each(sh.Holes.begin(), sh.Holes.end(), [&a](const PathImpl& h)
    {
        a -= ClipperLib::Area(h);
    });

    return -ClipperLib::Area(sh.Contour) + a;
}

template<>
inline double area<Orientation::COUNTER_CLOCKWISE>(const PolygonImpl& sh) {
    double a = 0;

    std::for_each(sh.Holes.begin(), sh.Holes.end(), [&a](const PathImpl& h)
    {
        a += ClipperLib::Area(h);
    });

    return ClipperLib::Area(sh.Contour) + a;
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

    // If the input is not at least a triangle, we can not do this algorithm
    if(sh.Contour.size() <= 3 ||
       std::any_of(sh.Holes.begin(), sh.Holes.end(),
                   [](const PathImpl& p) { return p.size() <= 3; })
       ) throw GeometryException(GeomErr::OFFSET);

    ClipperOffset offs;
    Paths result;
    offs.AddPath(sh.Contour, jtMiter, etClosedPolygon);
    offs.AddPaths(sh.Holes, jtMiter, etClosedPolygon);
    offs.Execute(result, static_cast<double>(distance));

    // Offsetting reverts the orientation and also removes the last vertex
    // so boost will not have a closed polygon.

    bool found_the_contour = false;
    for(auto& r : result) {
        if(ClipperLib::Orientation(r)) {
            // We don't like if the offsetting generates more than one contour
            // but throwing would be an overkill. Instead, we should warn the
            // caller about the inability to create correct geometries
            if(!found_the_contour) {
                sh.Contour = r;
                ClipperLib::ReversePath(sh.Contour);
                sh.Contour.push_back(sh.Contour.front());
                found_the_contour = true;
            } else {
                dout() << "Warning: offsetting result is invalid!";
                /* TODO warning */
            }
        } else {
            // TODO If there are multiple contours we can't be sure which hole
            // belongs to the first contour. (But in this case the situation is
            // bad enough to let it go...)
            sh.Holes.push_back(r);
            ClipperLib::ReversePath(sh.Holes.back());
            sh.Holes.back().push_back(sh.Holes.back().front());
        }
    }
}

// Tell libnest2d how to make string out of a ClipperPolygon object
template<> inline std::string ShapeLike::toString(const PolygonImpl& sh) {
    std::stringstream ss;

    ss << "Contour {\n";
    for(auto p : sh.Contour) {
        ss << "\t" << p.X << " " << p.Y << "\n";
    }
    ss << "}\n";

    for(auto& h : sh.Holes) {
        ss << "Holes {\n";
        for(auto p : h)  {
            ss << "\t{\n";
            ss << "\t\t" << p.X << " " << p.Y << "\n";
            ss << "\t}\n";
        }
        ss << "}\n";
    }

    return ss.str();
}

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

template<> inline PolygonImpl ShapeLike::create(const PathImpl& path,
                                                const HoleStore& holes) {
    PolygonImpl p;
    p.Contour = path;

    // Expecting that the coordinate system Y axis is positive in upwards
    // direction
    if(ClipperLib::Orientation(p.Contour)) {
        // Not clockwise then reverse the b*tch
        ClipperLib::ReversePath(p.Contour);
    }

    p.Holes = holes;
    for(auto& h : p.Holes) {
        if(!ClipperLib::Orientation(h)) {
            ClipperLib::ReversePath(h);
        }
    }

    return p;
}

template<> inline PolygonImpl ShapeLike::create( PathImpl&& path,
                                                 HoleStore&& holes) {
    PolygonImpl p;
    p.Contour.swap(path);

    // Expecting that the coordinate system Y axis is positive in upwards
    // direction
    if(ClipperLib::Orientation(p.Contour)) {
        // Not clockwise then reverse the b*tch
        ClipperLib::ReversePath(p.Contour);
    }

    p.Holes.swap(holes);

    for(auto& h : p.Holes) {
        if(!ClipperLib::Orientation(h)) {
            ClipperLib::ReversePath(h);
        }
    }

    return p;
}

template<> inline const THolesContainer<PolygonImpl>&
ShapeLike::holes(const PolygonImpl& sh)
{
    return sh.Holes;
}

template<> inline THolesContainer<PolygonImpl>&
ShapeLike::holes(PolygonImpl& sh)
{
    return sh.Holes;
}

template<> inline TContour<PolygonImpl>&
ShapeLike::getHole(PolygonImpl& sh, unsigned long idx)
{
    return sh.Holes[idx];
}

template<> inline const TContour<PolygonImpl>&
ShapeLike::getHole(const PolygonImpl& sh, unsigned long idx)
{
    return sh.Holes[idx];
}

template<> inline size_t ShapeLike::holeCount(const PolygonImpl& sh)
{
    return sh.Holes.size();
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
    for(auto& hole : sh.Holes) for(auto& p : hole) { p += offs; }
}

#define DISABLE_BOOST_ROTATE
template<>
inline void ShapeLike::rotate(PolygonImpl& sh, const Radians& rads)
{
    using Coord = TCoord<PointImpl>;

    auto cosa = rads.cos();
    auto sina = rads.sin();

    for(auto& p : sh.Contour) {
        p = {
                static_cast<Coord>(p.X * cosa - p.Y * sina),
                static_cast<Coord>(p.X * sina + p.Y * cosa)
            };
    }
    for(auto& hole : sh.Holes) for(auto& p : hole) {
        p = {
                static_cast<Coord>(p.X * cosa - p.Y * sina),
                static_cast<Coord>(p.X * sina + p.Y * cosa)
            };
    }
}

#define DISABLE_BOOST_NFP_MERGE
inline Nfp::Shapes<PolygonImpl> _merge(ClipperLib::Clipper& clipper) {
    Nfp::Shapes<PolygonImpl> retv;

    ClipperLib::PolyTree result;
    clipper.Execute(ClipperLib::ctUnion, result, ClipperLib::pftNegative);
    retv.reserve(static_cast<size_t>(result.Total()));

    std::function<void(ClipperLib::PolyNode*, PolygonImpl&)> processHole;

    auto processPoly = [&retv, &processHole](ClipperLib::PolyNode *pptr) {
        PolygonImpl poly(pptr->Contour);
        poly.Contour.push_back(poly.Contour.front());
        for(auto h : pptr->Childs) { processHole(h, poly); }
        retv.push_back(poly);
    };

    processHole = [&processPoly](ClipperLib::PolyNode *pptr, PolygonImpl& poly)
    {
        poly.Holes.push_back(pptr->Contour);
        poly.Holes.back().push_back(poly.Holes.back().front());
        for(auto c : pptr->Childs) processPoly(c);
    };

    auto traverse = [&processPoly] (ClipperLib::PolyNode *node)
    {
        for(auto ch : node->Childs) {
            processPoly(ch);
        }
    };

    traverse(&result);

    return retv;
}

template<> inline Nfp::Shapes<PolygonImpl>
Nfp::merge(const Nfp::Shapes<PolygonImpl>& shapes)
{
    ClipperLib::Clipper clipper(ClipperLib::ioReverseSolution);

    bool closed = true;
    bool valid = true;

    for(auto& path : shapes) {
        valid &= clipper.AddPath(path.Contour, ClipperLib::ptSubject, closed);

        for(auto& hole : path.Holes) {
            valid &= clipper.AddPath(hole, ClipperLib::ptSubject, closed);
        }
    }

    if(!valid) throw GeometryException(GeomErr::MERGE);

    return _merge(clipper);
}

}

//#define DISABLE_BOOST_SERIALIZE
//#define DISABLE_BOOST_UNSERIALIZE

// All other operators and algorithms are implemented with boost
#include "../boost_alg.hpp"

#endif // CLIPPER_BACKEND_HPP
