#ifndef CLIPPER_BACKEND_HPP
#define CLIPPER_BACKEND_HPP

#include <sstream>
#include <unordered_map>
#include <cassert>
#include <vector>
#include <iostream>

#include <libnest2d/geometry_traits.hpp>
#include <libnest2d/geometry_traits_nfp.hpp>

#include "clipper_polygon.hpp"

namespace libnest2d {

// Aliases for convinience
using PointImpl = ClipperLib::IntPoint;
using PathImpl  = ClipperLib::Path;
using HoleStore = ClipperLib::Paths;
using PolygonImpl = ClipperLib::Polygon;

// Type of coordinate units used by Clipper
template<> struct CoordType<PointImpl> {
    using Type = ClipperLib::cInt;
};

// Type of point used by Clipper
template<> struct PointType<PolygonImpl> {
    using Type = PointImpl;
};

template<> struct PointType<PathImpl> {
    using Type = PointImpl;
};

template<> struct PointType<PointImpl> {
    using Type = PointImpl;
};

template<> struct CountourType<PolygonImpl> {
    using Type = PathImpl;
};

template<> struct ShapeTag<PolygonImpl> { using Type = PolygonTag; };
template<> struct ShapeTag<PathImpl> { using Type = PathTag; };
template<> struct ShapeTag<PointImpl> { using Type = PointTag; };

template<> struct ShapeTag<TMultiShape<PolygonImpl>> {
    using Type = MultiPolygonTag;
};

template<> struct PointType<TMultiShape<PolygonImpl>> {
    using Type = PointImpl;
};

template<> struct HolesContainer<PolygonImpl> {
    using Type = ClipperLib::Paths;
};

namespace pointlike {

// Tell libnest2d how to extract the X coord from a ClipperPoint object
template<> inline TCoord<PointImpl> x(const PointImpl& p)
{
    return p.X;
}

// Tell libnest2d how to extract the Y coord from a ClipperPoint object
template<> inline TCoord<PointImpl> y(const PointImpl& p)
{
    return p.Y;
}

// Tell libnest2d how to extract the X coord from a ClipperPoint object
template<> inline TCoord<PointImpl>& x(PointImpl& p)
{
    return p.X;
}

// Tell libnest2d how to extract the Y coord from a ClipperPoint object
template<> inline TCoord<PointImpl>& y(PointImpl& p)
{
    return p.Y;
}

}

#define DISABLE_BOOST_AREA

namespace _smartarea {

template<Orientation o>
inline double area(const PolygonImpl& /*sh*/) {
    return std::nan("");
}

template<>
inline double area<Orientation::COUNTER_CLOCKWISE>(const PolygonImpl& sh) {
    return std::accumulate(sh.Holes.begin(), sh.Holes.end(),
                           ClipperLib::Area(sh.Contour),
                           [](double a, const ClipperLib::Path& pt){
        return a + ClipperLib::Area(pt);
    });
}

template<>
inline double area<Orientation::CLOCKWISE>(const PolygonImpl& sh) {
    return -area<Orientation::COUNTER_CLOCKWISE>(sh);
}

}

namespace shapelike {

// Tell libnest2d how to make string out of a ClipperPolygon object
template<> inline double area(const PolygonImpl& sh, const PolygonTag&)
{
    return _smartarea::area<OrientationType<PolygonImpl>::Value>(sh);
}

template<> inline void offset(PolygonImpl& sh, TCoord<PointImpl> distance)
{
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
                sh.Contour = std::move(r);
                ClipperLib::ReversePath(sh.Contour);
                auto front_p = sh.Contour.front();
                sh.Contour.emplace_back(std::move(front_p));
                found_the_contour = true;
            } else {
                dout() << "Warning: offsetting result is invalid!";
                /* TODO warning */
            }
        } else {
            // TODO If there are multiple contours we can't be sure which hole
            // belongs to the first contour. (But in this case the situation is
            // bad enough to let it go...)
            sh.Holes.emplace_back(std::move(r));
            ClipperLib::ReversePath(sh.Holes.back());
            auto front_p = sh.Holes.back().front();
            sh.Holes.back().emplace_back(std::move(front_p));
        }
    }
}

// Tell libnest2d how to make string out of a ClipperPolygon object
template<> inline std::string toString(const PolygonImpl& sh)
{
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
inline PolygonImpl create(const PathImpl& path, const HoleStore& holes)
{
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

template<> inline PolygonImpl create( PathImpl&& path, HoleStore&& holes) {
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

template<>
inline const THolesContainer<PolygonImpl>& holes(const PolygonImpl& sh)
{
    return sh.Holes;
}

template<> inline THolesContainer<PolygonImpl>& holes(PolygonImpl& sh)
{
    return sh.Holes;
}

template<>
inline TContour<PolygonImpl>& hole(PolygonImpl& sh, unsigned long idx)
{
    return sh.Holes[idx];
}

template<>
inline const TContour<PolygonImpl>& hole(const PolygonImpl& sh,
                                            unsigned long idx)
{
    return sh.Holes[idx];
}

template<> inline size_t holeCount(const PolygonImpl& sh)
{
    return sh.Holes.size();
}

template<> inline PathImpl& contour(PolygonImpl& sh)
{
    return sh.Contour;
}

template<>
inline const PathImpl& contour(const PolygonImpl& sh)
{
    return sh.Contour;
}

#define DISABLE_BOOST_TRANSLATE
template<>
inline void translate(PolygonImpl& sh, const PointImpl& offs)
{
    for(auto& p : sh.Contour) { p += offs; }
    for(auto& hole : sh.Holes) for(auto& p : hole) { p += offs; }
}

#define DISABLE_BOOST_ROTATE
template<>
inline void rotate(PolygonImpl& sh, const Radians& rads)
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

} // namespace shapelike

#define DISABLE_BOOST_NFP_MERGE
inline std::vector<PolygonImpl> clipper_execute(
        ClipperLib::Clipper& clipper,
        ClipperLib::ClipType clipType,
        ClipperLib::PolyFillType subjFillType = ClipperLib::pftEvenOdd,
        ClipperLib::PolyFillType clipFillType = ClipperLib::pftEvenOdd)
{
    shapelike::Shapes<PolygonImpl> retv;

    ClipperLib::PolyTree result;
    clipper.Execute(clipType, result, subjFillType, clipFillType);

    retv.reserve(static_cast<size_t>(result.Total()));

    std::function<void(ClipperLib::PolyNode*, PolygonImpl&)> processHole;

    auto processPoly = [&retv, &processHole](ClipperLib::PolyNode *pptr) {
        PolygonImpl poly;
        poly.Contour.swap(pptr->Contour);

        assert(!pptr->IsHole());

        if(pptr->IsOpen()) {
            auto front_p = poly.Contour.front();
            poly.Contour.emplace_back(front_p);
        }

        for(auto h : pptr->Childs) { processHole(h, poly); }
        retv.push_back(poly);
    };

    processHole = [&processPoly](ClipperLib::PolyNode *pptr, PolygonImpl& poly)
    {
        poly.Holes.emplace_back(std::move(pptr->Contour));

        assert(pptr->IsHole());

        if(pptr->IsOpen()) {
            auto front_p = poly.Holes.back().front();
            poly.Holes.back().emplace_back(front_p);
        }

        for(auto c : pptr->Childs) processPoly(c);
    };

    auto traverse = [&processPoly] (ClipperLib::PolyNode *node)
    {
        for(auto ch : node->Childs) processPoly(ch);
    };

    traverse(&result);

    return retv;
}

namespace nfp {

template<> inline std::vector<PolygonImpl>
merge(const std::vector<PolygonImpl>& shapes)
{
    ClipperLib::Clipper clipper(ClipperLib::ioReverseSolution);

    bool closed = true;
    bool valid = true;

    for(auto& path : shapes) {
        valid &= clipper.AddPath(path.Contour, ClipperLib::ptSubject, closed);

        for(auto& h : path.Holes)
            valid &= clipper.AddPath(h, ClipperLib::ptSubject, closed);
    }

    if(!valid) throw GeometryException(GeomErr::MERGE);

    return clipper_execute(clipper, ClipperLib::ctUnion, ClipperLib::pftNegative);
}

}

}

//#define DISABLE_BOOST_SERIALIZE
//#define DISABLE_BOOST_UNSERIALIZE

// All other operators and algorithms are implemented with boost
#include <libnest2d/utils/boost_alg.hpp>

#endif // CLIPPER_BACKEND_HPP
