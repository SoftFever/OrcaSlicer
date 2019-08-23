#include "MinAreaBoundingBox.hpp"

#include <libslic3r/ExPolygon.hpp>

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

#include <boost/rational.hpp>

#include <libslic3r/Int128.hpp>

#if !defined(HAS_INTRINSIC_128_TYPE) || defined(__APPLE__)
#include <boost/multiprecision/integer.hpp>
#endif

#include <libnest2d/geometry_traits.hpp>
#include <libnest2d/utils/rotcalipers.hpp>

namespace libnest2d {

template<> struct PointType<Slic3r::Points>      { using Type = Slic3r::Point; };
template<> struct CoordType<Slic3r::Point>       { using Type = coord_t; };
template<> struct ShapeTag<Slic3r::ExPolygon>    { using Type = PolygonTag; };
template<> struct ShapeTag<Slic3r::Polygon>      { using Type = PolygonTag; };
template<> struct ShapeTag<Slic3r::Points>       { using Type = PathTag; };
template<> struct ShapeTag<Slic3r::Point>        { using Type = PointTag; };
template<> struct ContourType<Slic3r::ExPolygon> { using Type = Slic3r::Points; };
template<> struct ContourType<Slic3r::Polygon>   { using Type = Slic3r::Points; };

namespace pointlike {

template<> inline coord_t x(const Slic3r::Point& p) { return p.x(); }
template<> inline coord_t y(const Slic3r::Point& p) { return p.y(); }
template<> inline coord_t& x(Slic3r::Point& p)      { return p.x(); }
template<> inline coord_t& y(Slic3r::Point& p)      { return p.y(); }

} // pointlike

namespace shapelike {
template<> inline Slic3r::Points& contour(Slic3r::ExPolygon& sh) { return sh.contour.points; }
template<> inline const Slic3r::Points& contour(const Slic3r::ExPolygon& sh) { return sh.contour.points; }
template<> inline Slic3r::Points& contour(Slic3r::Polygon& sh) { return sh.points; }
template<> inline const Slic3r::Points& contour(const Slic3r::Polygon& sh) { return sh.points; }

template<> Slic3r::Points::iterator begin(Slic3r::Points& pts, const PathTag&) { return pts.begin();}
template<> Slic3r::Points::const_iterator cbegin(const Slic3r::Points& pts, const PathTag&) { return pts.cbegin(); }
template<> Slic3r::Points::iterator end(Slic3r::Points& pts, const PathTag&) { return pts.end();}
template<> Slic3r::Points::const_iterator cend(const Slic3r::Points& pts, const PathTag&) { return pts.cend(); }

template<> inline Slic3r::ExPolygon create<Slic3r::ExPolygon>(Slic3r::Points&& contour)
{
    Slic3r::ExPolygon expoly; expoly.contour.points.swap(contour);
    return expoly;
}

template<> inline Slic3r::Polygon create<Slic3r::Polygon>(Slic3r::Points&& contour)
{
    Slic3r::Polygon poly; poly.points.swap(contour);
    return poly;
}

} // shapelike
} // libnest2d

namespace Slic3r {

// Used as compute type.
using Unit = int64_t;

#if !defined(HAS_INTRINSIC_128_TYPE) || defined(__APPLE__)
using Rational = boost::rational<boost::multiprecision::int128_t>;
#else
using Rational = boost::rational<__int128>;
#endif

MinAreaBoundigBox::MinAreaBoundigBox(const Polygon &p, PolygonLevel pc)
{
    const Polygon &chull = pc == pcConvex ? p :
                                            libnest2d::sl::convexHull(p);

    libnest2d::RotatedBox<Point, Unit> box =
        libnest2d::minAreaBoundingBox<Polygon, Unit, Rational>(chull);

    m_right  = libnest2d::cast<long double>(box.right_extent());
    m_bottom = libnest2d::cast<long double>(box.bottom_extent());
    m_axis   = box.axis();
}

MinAreaBoundigBox::MinAreaBoundigBox(const ExPolygon &p, PolygonLevel pc)
{
    const ExPolygon &chull = pc == pcConvex ? p :
                                              libnest2d::sl::convexHull(p);

    libnest2d::RotatedBox<Point, Unit> box =
        libnest2d::minAreaBoundingBox<ExPolygon, Unit, Rational>(chull);

    m_right  = libnest2d::cast<long double>(box.right_extent());
    m_bottom = libnest2d::cast<long double>(box.bottom_extent());
    m_axis   = box.axis();
}

MinAreaBoundigBox::MinAreaBoundigBox(const Points &pts, PolygonLevel pc)
{
    const Points &chull = pc == pcConvex ? pts :
                                           libnest2d::sl::convexHull(pts);

    libnest2d::RotatedBox<Point, Unit> box =
        libnest2d::minAreaBoundingBox<Points, Unit, Rational>(chull);

    m_right  = libnest2d::cast<long double>(box.right_extent());
    m_bottom = libnest2d::cast<long double>(box.bottom_extent());
    m_axis   = box.axis();
}

double MinAreaBoundigBox::angle_to_X() const
{
    double ret = std::atan2(m_axis.y(), m_axis.x());
    auto   s   = std::signbit(ret);
    if (s) ret += 2 * PI;
    return -ret;
}

long double MinAreaBoundigBox::width() const
{
    return std::abs(m_bottom) /
           std::sqrt(libnest2d::pl::magnsq<Point, long double>(m_axis));
}

long double MinAreaBoundigBox::height() const
{
    return std::abs(m_right) /
           std::sqrt(libnest2d::pl::magnsq<Point, long double>(m_axis));
}

long double MinAreaBoundigBox::area() const
{
    long double asq = libnest2d::pl::magnsq<Point, long double>(m_axis);
    return m_bottom * m_right / asq;
}

void remove_collinear_points(Polygon &p)
{
    p = libnest2d::removeCollinearPoints<Polygon>(p, Unit(0));
}

void remove_collinear_points(ExPolygon &p)
{
    p = libnest2d::removeCollinearPoints<ExPolygon>(p, Unit(0));
}
} // namespace Slic3r
