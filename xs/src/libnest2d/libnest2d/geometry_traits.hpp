#ifndef GEOMETRY_TRAITS_HPP
#define GEOMETRY_TRAITS_HPP

#include <string>
#include <type_traits>
#include <algorithm>
#include <array>
#include <vector>
#include <numeric>
#include <limits>
#include <cmath>

#include "common.hpp"

namespace libnest2d {

/// Getting the coordinate data type for a geometry class.
template<class GeomClass> struct CoordType { using Type = long; };

/// TCoord<GeomType> as shorthand for typename `CoordType<GeomType>::Type`.
template<class GeomType>
using TCoord = typename CoordType<remove_cvref_t<GeomType>>::Type;

/// Getting the type of point structure used by a shape.
template<class Shape> struct PointType { /*using Type = void;*/ };

/// TPoint<ShapeClass> as shorthand for `typename PointType<ShapeClass>::Type`.
template<class Shape>
using TPoint = typename PointType<remove_cvref_t<Shape>>::Type;

/// Getting the VertexIterator type of a shape class.
template<class Shape> struct VertexIteratorType { /*using Type = void;*/ };

/// Getting the const vertex iterator for a shape class.
template<class Shape> struct VertexConstIteratorType {/* using Type = void;*/ };

/**
 * TVertexIterator<Shape> as shorthand for
 * `typename VertexIteratorType<Shape>::Type`
 */
template<class Shape>
using TVertexIterator =
typename VertexIteratorType<remove_cvref_t<Shape>>::Type;

/**
 * \brief TVertexConstIterator<Shape> as shorthand for
 * `typename VertexConstIteratorType<Shape>::Type`
 */
template<class ShapeClass>
using TVertexConstIterator =
typename VertexConstIteratorType<remove_cvref_t<ShapeClass>>::Type;

/**
 * \brief A point pair base class for other point pairs (segment, box, ...).
 * \tparam RawPoint The actual point type to use.
 */
template<class RawPoint>
struct PointPair {
    RawPoint p1;
    RawPoint p2;
};

/**
 * \brief An abstraction of a box;
 */
template<class RawPoint>
class _Box: PointPair<RawPoint> {
    using PointPair<RawPoint>::p1;
    using PointPair<RawPoint>::p2;
public:

    inline _Box() = default;
    inline _Box(const RawPoint& p, const RawPoint& pp):
        PointPair<RawPoint>({p, pp}) {}

    inline _Box(TCoord<RawPoint> width, TCoord<RawPoint> height):
        _Box(RawPoint{0, 0}, RawPoint{width, height}) {}

    inline const RawPoint& minCorner() const BP2D_NOEXCEPT { return p1; }
    inline const RawPoint& maxCorner() const BP2D_NOEXCEPT { return p2; }

    inline RawPoint& minCorner() BP2D_NOEXCEPT { return p1; }
    inline RawPoint& maxCorner() BP2D_NOEXCEPT { return p2; }

    inline TCoord<RawPoint> width() const BP2D_NOEXCEPT;
    inline TCoord<RawPoint> height() const BP2D_NOEXCEPT;

    inline RawPoint center() const BP2D_NOEXCEPT;

    inline double area() const BP2D_NOEXCEPT {
        return double(width()*height());
    }
};

template<class RawPoint>
class _Circle {
    RawPoint center_;
    double radius_ = 0;
public:

    _Circle() = default;

    _Circle(const RawPoint& center, double r): center_(center), radius_(r) {}

    inline const RawPoint& center() const BP2D_NOEXCEPT { return center_; }
    inline const void center(const RawPoint& c) { center_ = c; }

    inline double radius() const BP2D_NOEXCEPT { return radius_; }
    inline void radius(double r) { radius_ = r; }

    inline double area() const BP2D_NOEXCEPT {
        return 2.0*Pi*radius_;
    }
};

/**
 * \brief An abstraction of a directed line segment with two points.
 */
template<class RawPoint>
class _Segment: PointPair<RawPoint> {
    using PointPair<RawPoint>::p1;
    using PointPair<RawPoint>::p2;
    mutable Radians angletox_ = std::nan("");
public:

    inline _Segment() = default;

    inline _Segment(const RawPoint& p, const RawPoint& pp):
        PointPair<RawPoint>({p, pp}) {}

    /**
     * @brief Get the first point.
     * @return Returns the starting point.
     */
    inline const RawPoint& first() const BP2D_NOEXCEPT { return p1; }

    /**
     * @brief The end point.
     * @return Returns the end point of the segment.
     */
    inline const RawPoint& second() const BP2D_NOEXCEPT { return p2; }

    inline void first(const RawPoint& p) BP2D_NOEXCEPT
    {
        angletox_ = std::nan(""); p1 = p;
    }

    inline void second(const RawPoint& p) BP2D_NOEXCEPT {
        angletox_ = std::nan(""); p2 = p;
    }

    /// Returns the angle measured to the X (horizontal) axis.
    inline Radians angleToXaxis() const;

    /// The length of the segment in the measure of the coordinate system.
    inline double length();
};

// This struct serves as a namespace. The only difference is that is can be
// used in friend declarations.
struct PointLike {

    template<class RawPoint>
    static TCoord<RawPoint> x(const RawPoint& p)
    {
        return p.x();
    }

    template<class RawPoint>
    static TCoord<RawPoint> y(const RawPoint& p)
    {
        return p.y();
    }

    template<class RawPoint>
    static TCoord<RawPoint>& x(RawPoint& p)
    {
        return p.x();
    }

    template<class RawPoint>
    static TCoord<RawPoint>& y(RawPoint& p)
    {
        return p.y();
    }

    template<class RawPoint>
    static double distance(const RawPoint& /*p1*/, const RawPoint& /*p2*/)
    {
        static_assert(always_false<RawPoint>::value,
                      "PointLike::distance(point, point) unimplemented!");
        return 0;
    }

    template<class RawPoint>
    static double distance(const RawPoint& /*p1*/,
                           const _Segment<RawPoint>& /*s*/)
    {
        static_assert(always_false<RawPoint>::value,
                      "PointLike::distance(point, segment) unimplemented!");
        return 0;
    }

    template<class RawPoint>
    static std::pair<TCoord<RawPoint>, bool> horizontalDistance(
            const RawPoint& p, const _Segment<RawPoint>& s)
    {
        using Unit = TCoord<RawPoint>;
        auto x = PointLike::x(p), y = PointLike::y(p);
        auto x1 = PointLike::x(s.first()), y1 = PointLike::y(s.first());
        auto x2 = PointLike::x(s.second()), y2 = PointLike::y(s.second());

        TCoord<RawPoint> ret;

        if( (y < y1 && y < y2) || (y > y1 && y > y2) )
            return {0, false};
        if ((y == y1 && y == y2) && (x > x1 && x > x2))
            ret = std::min( x-x1, x -x2);
        else if( (y == y1 && y == y2) && (x < x1 && x < x2))
            ret = -std::min(x1 - x, x2 - x);
        else if(std::abs(y - y1) <= std::numeric_limits<Unit>::epsilon() &&
                std::abs(y - y2) <= std::numeric_limits<Unit>::epsilon())
            ret = 0;
        else
            ret = x - x1 + (x1 - x2)*(y1 - y)/(y1 - y2);

        return {ret, true};
    }

    template<class RawPoint>
    static std::pair<TCoord<RawPoint>, bool> verticalDistance(
            const RawPoint& p, const _Segment<RawPoint>& s)
    {
        using Unit = TCoord<RawPoint>;
        auto x = PointLike::x(p), y = PointLike::y(p);
        auto x1 = PointLike::x(s.first()), y1 = PointLike::y(s.first());
        auto x2 = PointLike::x(s.second()), y2 = PointLike::y(s.second());

        TCoord<RawPoint> ret;

        if( (x < x1 && x < x2) || (x > x1 && x > x2) )
            return {0, false};
        if ((x == x1 && x == x2) && (y > y1 && y > y2))
            ret = std::min( y-y1, y -y2);
        else if( (x == x1 && x == x2) && (y < y1 && y < y2))
            ret = -std::min(y1 - y, y2 - y);
        else if(std::abs(x - x1) <= std::numeric_limits<Unit>::epsilon() &&
                std::abs(x - x2) <= std::numeric_limits<Unit>::epsilon())
            ret = 0;
        else
            ret = y - y1 + (y1 - y2)*(x1 - x)/(x1 - x2);

        return {ret, true};
    }
};

template<class RawPoint>
TCoord<RawPoint> _Box<RawPoint>::width() const BP2D_NOEXCEPT
{
    return PointLike::x(maxCorner()) - PointLike::x(minCorner());
}

template<class RawPoint>
TCoord<RawPoint> _Box<RawPoint>::height() const BP2D_NOEXCEPT
{
    return PointLike::y(maxCorner()) - PointLike::y(minCorner());
}

template<class RawPoint>
TCoord<RawPoint> getX(const RawPoint& p) { return PointLike::x<RawPoint>(p); }

template<class RawPoint>
TCoord<RawPoint> getY(const RawPoint& p) { return PointLike::y<RawPoint>(p); }

template<class RawPoint>
void setX(RawPoint& p, const TCoord<RawPoint>& val)
{
    PointLike::x<RawPoint>(p) = val;
}

template<class RawPoint>
void setY(RawPoint& p, const TCoord<RawPoint>& val)
{
    PointLike::y<RawPoint>(p) = val;
}

template<class RawPoint>
inline Radians _Segment<RawPoint>::angleToXaxis() const
{
    if(std::isnan(static_cast<double>(angletox_))) {
        TCoord<RawPoint> dx = getX(second()) - getX(first());
        TCoord<RawPoint> dy = getY(second()) - getY(first());

        double a = std::atan2(dy, dx);
        auto s = std::signbit(a);

        if(s) a += Pi_2;
        angletox_ = a;
    }
    return angletox_;
}

template<class RawPoint>
inline double _Segment<RawPoint>::length()
{
    return PointLike::distance(first(), second());
}

template<class RawPoint>
inline RawPoint _Box<RawPoint>::center() const BP2D_NOEXCEPT {
    auto& minc = minCorner();
    auto& maxc = maxCorner();

    using Coord = TCoord<RawPoint>;

    RawPoint ret =  {
        static_cast<Coord>( std::round((getX(minc) + getX(maxc))/2.0) ),
        static_cast<Coord>( std::round((getY(minc) + getY(maxc))/2.0) )
    };

    return ret;
}

template<class RawShape>
struct HolesContainer {
    using Type = std::vector<RawShape>;
};

template<class RawShape>
using THolesContainer = typename HolesContainer<remove_cvref_t<RawShape>>::Type;

template<class RawShape>
struct CountourType {
    using Type = RawShape;
};

template<class RawShape>
using TContour = typename CountourType<remove_cvref_t<RawShape>>::Type;

enum class Orientation {
    CLOCKWISE,
    COUNTER_CLOCKWISE
};

template<class RawShape>
struct OrientationType {

    // Default Polygon orientation that the library expects
    static const Orientation Value = Orientation::CLOCKWISE;
};

enum class Formats {
    WKT,
    SVG
};

// This struct serves as a namespace. The only difference is that it can be
// used in friend declarations and can be aliased at class scope.
struct ShapeLike {

    template<class RawShape>
    using Shapes = std::vector<RawShape>;

    template<class RawShape>
    static RawShape create(const TContour<RawShape>& contour,
                           const THolesContainer<RawShape>& holes)
    {
        return RawShape(contour, holes);
    }

    template<class RawShape>
    static RawShape create(TContour<RawShape>&& contour,
                           THolesContainer<RawShape>&& holes)
    {
        return RawShape(contour, holes);
    }

    template<class RawShape>
    static RawShape create(const TContour<RawShape>& contour)
    {
        return create<RawShape>(contour, {});
    }

    template<class RawShape>
    static RawShape create(TContour<RawShape>&& contour)
    {
        return create<RawShape>(contour, {});
    }

    template<class RawShape>
    static THolesContainer<RawShape>& holes(RawShape& /*sh*/)
    {
        static THolesContainer<RawShape> empty;
        return empty;
    }

    template<class RawShape>
    static const THolesContainer<RawShape>& holes(const RawShape& /*sh*/)
    {
        static THolesContainer<RawShape> empty;
        return empty;
    }

    template<class RawShape>
    static TContour<RawShape>& getHole(RawShape& sh, unsigned long idx)
    {
        return holes(sh)[idx];
    }

    template<class RawShape>
    static const TContour<RawShape>& getHole(const RawShape& sh,
                                              unsigned long idx)
    {
        return holes(sh)[idx];
    }

    template<class RawShape>
    static size_t holeCount(const RawShape& sh)
    {
        return holes(sh).size();
    }

    template<class RawShape>
    static TContour<RawShape>& getContour(RawShape& sh)
    {
        return sh;
    }

    template<class RawShape>
    static const TContour<RawShape>& getContour(const RawShape& sh)
    {
        return sh;
    }

    // Optional, does nothing by default
    template<class RawShape>
    static void reserve(RawShape& /*sh*/,  size_t /*vertex_capacity*/) {}

    template<class RawShape, class...Args>
    static void addVertex(RawShape& sh, Args...args)
    {
        return getContour(sh).emplace_back(std::forward<Args>(args)...);
    }

    template<class RawShape>
    static TVertexIterator<RawShape> begin(RawShape& sh)
    {
        return sh.begin();
    }

    template<class RawShape>
    static TVertexIterator<RawShape> end(RawShape& sh)
    {
        return sh.end();
    }

    template<class RawShape>
    static TVertexConstIterator<RawShape> cbegin(const RawShape& sh)
    {
        return sh.cbegin();
    }

    template<class RawShape>
    static TVertexConstIterator<RawShape> cend(const RawShape& sh)
    {
        return sh.cend();
    }

    template<class RawShape>
    static std::string toString(const RawShape& /*sh*/)
    {
        return "";
    }

    template<Formats, class RawShape>
    static std::string serialize(const RawShape& /*sh*/, double /*scale*/=1)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::serialize() unimplemented!");
        return "";
    }

    template<Formats, class RawShape>
    static void unserialize(RawShape& /*sh*/, const std::string& /*str*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::unserialize() unimplemented!");
    }

    template<class RawShape>
    static double area(const RawShape& /*sh*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::area() unimplemented!");
        return 0;
    }

    template<class RawShape>
    static bool intersects(const RawShape& /*sh*/, const RawShape& /*sh*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::intersects() unimplemented!");
        return false;
    }

    template<class RawShape>
    static bool isInside(const TPoint<RawShape>& /*point*/,
                         const RawShape& /*shape*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::isInside(point, shape) unimplemented!");
        return false;
    }

    template<class RawShape>
    static bool isInside(const RawShape& /*shape*/,
                         const RawShape& /*shape*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::isInside(shape, shape) unimplemented!");
        return false;
    }

    template<class RawShape>
    static bool touches( const RawShape& /*shape*/,
                         const RawShape& /*shape*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::touches(shape, shape) unimplemented!");
        return false;
    }

    template<class RawShape>
    static bool touches( const TPoint<RawShape>& /*point*/,
                         const RawShape& /*shape*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::touches(point, shape) unimplemented!");
        return false;
    }

    template<class RawShape>
    static _Box<TPoint<RawShape>> boundingBox(const RawShape& /*sh*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::boundingBox(shape) unimplemented!");
    }

    template<class RawShape>
    static _Box<TPoint<RawShape>> boundingBox(const Shapes<RawShape>& /*sh*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::boundingBox(shapes) unimplemented!");
    }

    template<class RawShape>
    static RawShape convexHull(const RawShape& /*sh*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::convexHull(shape) unimplemented!");
        return RawShape();
    }

    template<class RawShape>
    static RawShape convexHull(const Shapes<RawShape>& /*sh*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::convexHull(shapes) unimplemented!");
        return RawShape();
    }

    template<class RawShape>
    static void rotate(RawShape& /*sh*/, const Radians& /*rads*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::rotate() unimplemented!");
    }

    template<class RawShape, class RawPoint>
    static void translate(RawShape& /*sh*/, const RawPoint& /*offs*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::translate() unimplemented!");
    }

    template<class RawShape>
    static void offset(RawShape& /*sh*/, TCoord<TPoint<RawShape>> /*distance*/)
    {
        static_assert(always_false<RawShape>::value,
                      "ShapeLike::offset() unimplemented!");
    }

    template<class RawShape>
    static std::pair<bool, std::string> isValid(const RawShape& /*sh*/)
    {
        return {false, "ShapeLike::isValid() unimplemented!"};
    }

    template<class RawShape>
    static inline bool isConvex(const TContour<RawShape>& sh)
    {
        using Vertex = TPoint<RawShape>;
        auto first = sh.begin();
        auto middle = std::next(first);
        auto last = std::next(middle);
        using CVrRef = const Vertex&;

        auto zcrossproduct = [](CVrRef k, CVrRef k1, CVrRef k2) {
            auto dx1 = getX(k1) - getX(k);
            auto dy1 = getY(k1) - getY(k);
            auto dx2 = getX(k2) - getX(k1);
            auto dy2 = getY(k2) - getY(k1);
            return dx1*dy2 - dy1*dx2;
        };

        auto firstprod = zcrossproduct( *(std::prev(std::prev(sh.end()))),
                                        *first,
                                        *middle );

        bool ret = true;
        bool frsign = firstprod > 0;
        while(last != sh.end()) {
            auto &k = *first, &k1 = *middle, &k2 = *last;
            auto zc = zcrossproduct(k, k1, k2);
            ret &= frsign == (zc > 0);
            ++first; ++middle; ++last;
        }

        return ret;
    }

    // *************************************************************************
    // No need to implement these
    // *************************************************************************

    template<class RawShape>
    static inline _Box<TPoint<RawShape>> boundingBox(
            const _Box<TPoint<RawShape>>& box)
    {
        return box;
    }

    template<class RawShape>
    static inline _Box<TPoint<RawShape>> boundingBox(
            const _Circle<TPoint<RawShape>>& circ)
    {
        using Coord = TCoord<TPoint<RawShape>>;
        TPoint<RawShape> pmin = {
            static_cast<Coord>(getX(circ.center()) - circ.radius()),
            static_cast<Coord>(getY(circ.center()) - circ.radius()) };

        TPoint<RawShape> pmax = {
            static_cast<Coord>(getX(circ.center()) + circ.radius()),
            static_cast<Coord>(getY(circ.center()) + circ.radius()) };

        return {pmin, pmax};
    }

    template<class RawShape>
    static inline double area(const _Box<TPoint<RawShape>>& box)
    {
        return static_cast<double>(box.width() * box.height());
    }

    template<class RawShape>
    static inline double area(const _Circle<TPoint<RawShape>>& circ)
    {
        return circ.area();
    }

    template<class RawShape>
    static inline double area(const Shapes<RawShape>& shapes)
    {
        return std::accumulate(shapes.begin(), shapes.end(), 0.0,
                        [](double a, const RawShape& b) {
            return a += area(b);
        });
    }

    template<class RawShape>
    static bool isInside(const TPoint<RawShape>& point,
                         const _Circle<TPoint<RawShape>>& circ)
    {
        return PointLike::distance(point, circ.center()) < circ.radius();
    }

    template<class RawShape>
    static bool isInside(const TPoint<RawShape>& point,
                         const _Box<TPoint<RawShape>>& box)
    {
        auto px = getX(point);
        auto py = getY(point);
        auto minx = getX(box.minCorner());
        auto miny = getY(box.minCorner());
        auto maxx = getX(box.maxCorner());
        auto maxy = getY(box.maxCorner());

        return px > minx && px < maxx && py > miny && py < maxy;
    }

    template<class RawShape>
    static bool isInside(const RawShape& sh,
                         const _Circle<TPoint<RawShape>>& circ)
    {
        return std::all_of(cbegin(sh), cend(sh),
                           [&circ](const TPoint<RawShape>& p){
            return isInside<RawShape>(p, circ);
        });
    }

    template<class RawShape>
    static bool isInside(const _Box<TPoint<RawShape>>& box,
                         const _Circle<TPoint<RawShape>>& circ)
    {
        return isInside<RawShape>(box.minCorner(), circ) &&
                isInside<RawShape>(box.maxCorner(), circ);
    }

    template<class RawShape>
    static bool isInside(const _Box<TPoint<RawShape>>& ibb,
                         const _Box<TPoint<RawShape>>& box)
    {
        auto iminX = getX(ibb.minCorner());
        auto imaxX = getX(ibb.maxCorner());
        auto iminY = getY(ibb.minCorner());
        auto imaxY = getY(ibb.maxCorner());

        auto minX = getX(box.minCorner());
        auto maxX = getX(box.maxCorner());
        auto minY = getY(box.minCorner());
        auto maxY = getY(box.maxCorner());

        return iminX > minX && imaxX < maxX && iminY > minY && imaxY < maxY;
    }

    template<class RawShape> // Potential O(1) implementation may exist
    static inline TPoint<RawShape>& vertex(RawShape& sh, unsigned long idx)
    {
        return *(begin(sh) + idx);
    }

    template<class RawShape> // Potential O(1) implementation may exist
    static inline const TPoint<RawShape>& vertex(const RawShape& sh,
                                          unsigned long idx)
    {
        return *(cbegin(sh) + idx);
    }

    template<class RawShape>
    static inline size_t contourVertexCount(const RawShape& sh)
    {
        return cend(sh) - cbegin(sh);
    }

    template<class RawShape, class Fn>
    static inline void foreachContourVertex(RawShape& sh, Fn fn) {
        for(auto it = begin(sh); it != end(sh); ++it) fn(*it);
    }

    template<class RawShape, class Fn>
    static inline void foreachHoleVertex(RawShape& sh, Fn fn) {
        for(int i = 0; i < holeCount(sh); ++i) {
            auto& h = getHole(sh, i);
            for(auto it = begin(h); it != end(h); ++it) fn(*it);
        }
    }

    template<class RawShape, class Fn>
    static inline void foreachContourVertex(const RawShape& sh, Fn fn) {
        for(auto it = cbegin(sh); it != cend(sh); ++it) fn(*it);
    }

    template<class RawShape, class Fn>
    static inline void foreachHoleVertex(const RawShape& sh, Fn fn) {
        for(int i = 0; i < holeCount(sh); ++i) {
            auto& h = getHole(sh, i);
            for(auto it = cbegin(h); it != cend(h); ++it) fn(*it);
        }
    }

    template<class RawShape, class Fn>
    static inline void foreachVertex(RawShape& sh, Fn fn) {
        foreachContourVertex(sh, fn);
        foreachHoleVertex(sh, fn);
    }

    template<class RawShape, class Fn>
    static inline void foreachVertex(const RawShape& sh, Fn fn) {
        foreachContourVertex(sh, fn);
        foreachHoleVertex(sh, fn);
    }

};

}

#endif // GEOMETRY_TRAITS_HPP
