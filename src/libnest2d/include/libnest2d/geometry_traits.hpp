#ifndef GEOMETRY_TRAITS_HPP
#define GEOMETRY_TRAITS_HPP

#include <string>
#include <type_traits>
#include <algorithm>
#include <array>
#include <vector>
#include <numeric>
#include <iterator>
#include <cmath>
#include <cstdint>

#include <libnest2d/common.hpp>

namespace libnest2d {

// Meta tags for different geometry concepts. 
struct PointTag {};
struct PolygonTag {};
struct PathTag {};
struct MultiPolygonTag {};
struct BoxTag {};
struct CircleTag {};

/// Meta-function to derive the tag of a shape type.
template<class Shape> struct ShapeTag { using Type = typename Shape::Tag; };

/// Tag<S> will be used instead of `typename ShapeTag<S>::Type`
template<class S> using Tag = typename ShapeTag<remove_cvref_t<S>>::Type;

/// Meta function to derive the contour type for a polygon which could be itself
template<class RawShape> struct ContourType { using Type = RawShape; };

/// TContour<RawShape> instead of `typename ContourType<RawShape>::type`
template<class RawShape>
using TContour = typename ContourType<remove_cvref_t<RawShape>>::Type;

/// Getting the type of point structure used by a shape.
template<class Sh> struct PointType { 
    using Type = typename PointType<TContour<Sh>>::Type; 
};

/// TPoint<ShapeClass> as shorthand for `typename PointType<ShapeClass>::Type`.
template<class Shape>
using TPoint = typename PointType<remove_cvref_t<Shape>>::Type;

/// Getting the coordinate data type for a geometry class.
template<class GeomClass> struct CoordType { 
    using Type = typename CoordType<TPoint<GeomClass>>::Type; 
};

/// TCoord<GeomType> as shorthand for typename `CoordType<GeomType>::Type`.
template<class GeomType>
using TCoord = typename CoordType<remove_cvref_t<GeomType>>::Type;


/// Getting the computation type for a certain geometry type.
/// It is the coordinate type by default but it is advised that a type with
/// larger precision and (or) range is specified.
template<class T, bool = std::is_arithmetic<T>::value> struct ComputeType {};

/// A compute type is introduced to hold the results of computations on
/// coordinates and points. It should be larger in range than the coordinate 
/// type or the range of coordinates should be limited to not loose precision.
template<class GeomClass> struct ComputeType<GeomClass, false> {
    using Type = typename ComputeType<TCoord<GeomClass>>::Type;
};

/// libnest2d will choose a default compute type for various coordinate types
/// if the backend has not specified anything.
template<class T> struct DoublePrecision { using Type = T; };
template<> struct DoublePrecision<int8_t> { using Type = int16_t; };
template<> struct DoublePrecision<int16_t> { using Type = int32_t; };
template<> struct DoublePrecision<int32_t> { using Type = int64_t; };
template<> struct DoublePrecision<float> { using Type = double; };
template<> struct DoublePrecision<double> { using Type = long double; };
template<class I> struct ComputeType<I, true> {
    using Type = typename DoublePrecision<I>::Type;
};

/// TCompute<T> shorthand for `typename ComputeType<T>::Type`
template<class T> using TCompute = typename ComputeType<remove_cvref_t<T>>::Type;

/// A meta function to derive a container type for holes in a polygon
template<class RawShape>
struct HolesContainer { using Type = std::vector<TContour<RawShape>>;  };

/// Shorthand for `typename HolesContainer<RawShape>::Type`
template<class RawShape>
using THolesContainer = typename HolesContainer<remove_cvref_t<RawShape>>::Type;

/*
 * TContour, TPoint, TCoord and TCompute should be usable for any type for which
 * it makes sense. For example, the point type could be derived from the contour,
 * the polygon and (or) the multishape as well. The coordinate type also and
 * including the point type. TCoord<Polygon>, TCoord<Path>, TCoord<Point> are
 * all valid types and derives the coordinate type of template argument Polygon,
 * Path and Point. This is also true for TCompute, but it can also take the 
 * coordinate type as argument.
 */

/*
 * A Multi shape concept is also introduced. A multi shape is something that
 * can contain the result of an operation where the input is one polygon and 
 * the result could be many polygons or path -> paths. The MultiShape should be
 * a container type. If the backend does not specialize the MultiShape template,
 * a default multi shape container will be used.
 */

/// The default multi shape container.
template<class S> struct DefaultMultiShape: public std::vector<S> {
    using Tag = MultiPolygonTag;
    template<class...Args> DefaultMultiShape(Args&&...args):
        std::vector<S>(std::forward<Args>(args)...) {}
};

/// The MultiShape Type trait which gets the container type for a geometry type.
template<class S> struct MultiShape { using Type = DefaultMultiShape<S>; };

/// use TMultiShape<S> instead of `typename MultiShape<S>::Type`
template<class S> 
using TMultiShape = typename MultiShape<remove_cvref_t<S>>::Type;

// A specialization of ContourType to work with the default multishape type
template<class S> struct ContourType<DefaultMultiShape<S>> {
    using Type = typename ContourType<S>::Type;
};

enum class Orientation {
    CLOCKWISE,
    COUNTER_CLOCKWISE
};

template<class RawShape>
struct OrientationType {

    // Default Polygon orientation that the library expects
    static const Orientation Value = Orientation::CLOCKWISE;
};

template<class T> inline /*constexpr*/ bool is_clockwise() { 
    return OrientationType<TContour<T>>::Value == Orientation::CLOCKWISE; 
}


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

    using Tag = BoxTag;
    using PointType = RawPoint;

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

    template<class Unit = TCompute<RawPoint>> 
    inline Unit area() const BP2D_NOEXCEPT {
        return Unit(width())*height();
    }
};

template<class S> struct PointType<_Box<S>> { 
    using Type = typename _Box<S>::PointType; 
};

template<class RawPoint>
class _Circle {
    RawPoint center_;
    double radius_ = 0;
public:

    using Tag = CircleTag;
    using PointType = RawPoint;

    _Circle() = default;
    _Circle(const RawPoint& center, double r): center_(center), radius_(r) {}

    inline const RawPoint& center() const BP2D_NOEXCEPT { return center_; }
    inline void center(const RawPoint& c) { center_ = c; }

    inline double radius() const BP2D_NOEXCEPT { return radius_; }
    inline void radius(double r) { radius_ = r; }
    
    inline double area() const BP2D_NOEXCEPT {
        return Pi_2 * radius_ * radius_;
    }
};

template<class S> struct PointType<_Circle<S>> {
    using Type = typename _Circle<S>::PointType;
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

    using PointType = RawPoint;

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
    template<class Unit = TCompute<RawPoint>> inline Unit sqlength() const;
    
};

template<class S> struct PointType<_Segment<S>> { 
    using Type = typename _Circle<S>::PointType; 
};

// This struct serves almost as a namespace. The only difference is that is can
// used in friend declarations.
namespace pointlike {

template<class RawPoint>
inline TCoord<RawPoint> x(const RawPoint& p)
{
    return p.x();
}

template<class RawPoint>
inline TCoord<RawPoint> y(const RawPoint& p)
{
    return p.y();
}

template<class RawPoint>
inline TCoord<RawPoint>& x(RawPoint& p)
{
    return p.x();
}

template<class RawPoint>
inline TCoord<RawPoint>& y(RawPoint& p)
{
    return p.y();
}

template<class RawPoint, class Unit = TCompute<RawPoint>>
inline Unit squaredDistance(const RawPoint& p1, const RawPoint& p2)
{
    auto x1 = Unit(x(p1)), y1 = Unit(y(p1)), x2 = Unit(x(p2)), y2 = Unit(y(p2));
    Unit a = (x2 - x1), b = (y2 - y1);
    return a * a + b * b;
}

template<class RawPoint>
inline double distance(const RawPoint& p1, const RawPoint& p2)
{
    return std::sqrt(squaredDistance<RawPoint, double>(p1, p2));
}

// create perpendicular vector
template<class Pt> inline Pt perp(const Pt& p) 
{ 
    return Pt(y(p), -x(p));
}

template<class Pt, class Unit = TCompute<Pt>> 
inline Unit dotperp(const Pt& a, const Pt& b) 
{ 
    return Unit(x(a)) * Unit(y(b)) - Unit(y(a)) * Unit(x(b)); 
}

// dot product
template<class Pt, class Unit = TCompute<Pt>> 
inline Unit dot(const Pt& a, const Pt& b) 
{
    return Unit(x(a)) * x(b) + Unit(y(a)) * y(b);
}

// squared vector magnitude
template<class Pt, class Unit = TCompute<Pt>> 
inline Unit magnsq(const Pt& p) 
{
    return  Unit(x(p)) * x(p) + Unit(y(p)) * y(p);
}

template<class RawPoint, class Unit = TCompute<RawPoint>>
inline std::pair<Unit, bool> horizontalDistance(
        const RawPoint& p, const _Segment<RawPoint>& s)
{
    namespace pl = pointlike;
    auto x = Unit(pl::x(p)), y = Unit(pl::y(p));
    auto x1 = Unit(pl::x(s.first())), y1 = Unit(pl::y(s.first()));
    auto x2 = Unit(pl::x(s.second())), y2 = Unit(pl::y(s.second()));

    Unit ret;

    if( (y < y1 && y < y2) || (y > y1 && y > y2) )
        return {0, false};
    if ((y == y1 && y == y2) && (x > x1 && x > x2))
        ret = std::min( x-x1, x -x2);
    else if( (y == y1 && y == y2) && (x < x1 && x < x2))
        ret = -std::min(x1 - x, x2 - x);
    else if(y == y1 && y == y2)
        ret = 0;
    else
        ret = x - x1 + (x1 - x2)*(y1 - y)/(y1 - y2);

    return {ret, true};
}

template<class RawPoint, class Unit = TCompute<RawPoint>>
inline std::pair<Unit, bool> verticalDistance(
        const RawPoint& p, const _Segment<RawPoint>& s)
{
    namespace pl = pointlike;
    auto x = Unit(pl::x(p)), y = Unit(pl::y(p));
    auto x1 = Unit(pl::x(s.first())), y1 = Unit(pl::y(s.first()));
    auto x2 = Unit(pl::x(s.second())), y2 = Unit(pl::y(s.second()));

    Unit ret;

    if( (x < x1 && x < x2) || (x > x1 && x > x2) )
        return {0, false};
    if ((x == x1 && x == x2) && (y > y1 && y > y2))
        ret = std::min( y-y1, y -y2);
    else if( (x == x1 && x == x2) && (y < y1 && y < y2))
        ret = -std::min(y1 - y, y2 - y);
    else if(x == x1 && x == x2)
        ret = 0;
    else
        ret = y - y1 + (y1 - y2)*(x1 - x)/(x1 - x2);

    return {ret, true};
}
}

template<class RawPoint>
TCoord<RawPoint> _Box<RawPoint>::width() const BP2D_NOEXCEPT
{
    return pointlike::x(maxCorner()) - pointlike::x(minCorner());
}

template<class RawPoint>
TCoord<RawPoint> _Box<RawPoint>::height() const BP2D_NOEXCEPT
{
    return pointlike::y(maxCorner()) - pointlike::y(minCorner());
}

template<class RawPoint>
TCoord<RawPoint> getX(const RawPoint& p) { return pointlike::x<RawPoint>(p); }

template<class RawPoint>
TCoord<RawPoint> getY(const RawPoint& p) { return pointlike::y<RawPoint>(p); }

template<class RawPoint>
void setX(RawPoint& p, const TCoord<RawPoint>& val)
{
    pointlike::x<RawPoint>(p) = val;
}

template<class RawPoint>
void setY(RawPoint& p, const TCoord<RawPoint>& val)
{
    pointlike::y<RawPoint>(p) = val;
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
template<class Unit>
inline Unit _Segment<RawPoint>::sqlength() const
{
    return pointlike::squaredDistance<RawPoint, Unit>(first(), second());
}

template<class RawPoint>
inline RawPoint _Box<RawPoint>::center() const BP2D_NOEXCEPT {
    auto& minc = minCorner();
    auto& maxc = maxCorner();

    using Coord = TCoord<RawPoint>;

    RawPoint ret =  { // No rounding here, we dont know if these are int coords
        Coord( (getX(minc) + getX(maxc)) / Coord(2) ),
        Coord( (getY(minc) + getY(maxc)) / Coord(2) )
    };

    return ret;
}

enum class Formats {
    WKT,
    SVG
};

// This struct serves as a namespace. The only difference is that it can be
// used in friend declarations and can be aliased at class scope.
namespace shapelike {

template<class RawShape>
inline RawShape create(const TContour<RawShape>& contour,
                       const THolesContainer<RawShape>& holes)
{
    return RawShape(contour, holes);
}

template<class RawShape>
inline RawShape create(TContour<RawShape>&& contour,
                       THolesContainer<RawShape>&& holes)
{
    return RawShape(contour, holes);
}

template<class RawShape>
inline RawShape create(const TContour<RawShape>& contour)
{
    return create<RawShape>(contour, {});
}

template<class RawShape>
inline RawShape create(TContour<RawShape>&& contour)
{
    return create<RawShape>(contour, {});
}

template<class RawShape>
inline THolesContainer<RawShape>& holes(RawShape& /*sh*/)
{
    static THolesContainer<RawShape> empty;
    return empty;
}

template<class RawShape>
inline const THolesContainer<RawShape>& holes(const RawShape& /*sh*/)
{
    static THolesContainer<RawShape> empty;
    return empty;
}

template<class RawShape>
inline TContour<RawShape>& hole(RawShape& sh, unsigned long idx)
{
    return holes(sh)[idx];
}

template<class RawShape>
inline const TContour<RawShape>& hole(const RawShape& sh, unsigned long idx)
{
    return holes(sh)[idx];
}

template<class RawShape>
inline size_t holeCount(const RawShape& sh)
{
    return holes(sh).size();
}

template<class RawShape>
inline TContour<RawShape>& contour(RawShape& sh)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::contour() unimplemented!");
    return sh;
}

template<class RawShape>
inline const TContour<RawShape>& contour(const RawShape& sh)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::contour() unimplemented!");
    return sh;
}

// Optional, does nothing by default
template<class RawPath>
inline void reserve(RawPath& p, size_t vertex_capacity, const PathTag&)
{
    p.reserve(vertex_capacity);
}

template<class RawShape, class...Args>
inline void addVertex(RawShape& sh, const PathTag&, Args...args)
{
    sh.emplace_back(std::forward<Args>(args)...);
}

template<class RawShape, class Fn>
inline void foreachVertex(RawShape& sh, Fn fn, const PathTag&) {
    std::for_each(sh.begin(), sh.end(), fn);
}

template<class RawShape>
inline typename RawShape::iterator begin(RawShape& sh, const PathTag&)
{
    return sh.begin();
}

template<class RawShape>
inline typename RawShape::iterator end(RawShape& sh, const PathTag&)
{
    return sh.end();
}

template<class RawShape>
inline typename RawShape::const_iterator
cbegin(const RawShape& sh, const PathTag&)
{
    return sh.cbegin();
}

template<class RawShape>
inline typename RawShape::const_iterator
cend(const RawShape& sh, const PathTag&)
{
    return sh.cend();
}

template<class RawShape>
inline std::string toString(const RawShape& /*sh*/)
{
    return "";
}

template<Formats, class RawShape>
inline std::string serialize(const RawShape& /*sh*/, double /*scale*/=1)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::serialize() unimplemented!");
    return "";
}

template<Formats, class RawShape>
inline void unserialize(RawShape& /*sh*/, const std::string& /*str*/)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::unserialize() unimplemented!");
}

template<class Cntr, class Unit = double>
inline Unit area(const Cntr& poly, const PathTag& );

template<class RawShape>
inline bool intersects(const RawShape& /*sh*/, const RawShape& /*sh*/)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::intersects() unimplemented!");
    return false;
}

template<class TGuest, class THost>
inline bool isInside(const TGuest&, const THost&,
                     const PointTag&, const PolygonTag&) {
    static_assert(always_false<THost>::value,
                      "shapelike::isInside(point, path) unimplemented!");
    return false;
}

template<class TGuest, class THost>
inline bool isInside(const TGuest&, const THost&,
                     const PolygonTag&, const PolygonTag&) {
    static_assert(always_false<THost>::value,
                      "shapelike::isInside(shape, shape) unimplemented!");
    return false;
}

template<class RawShape>
inline bool touches( const RawShape& /*shape*/,
                     const RawShape& /*shape*/)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::touches(shape, shape) unimplemented!");
    return false;
}

template<class RawShape>
inline bool touches( const TPoint<RawShape>& /*point*/,
                     const RawShape& /*shape*/)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::touches(point, shape) unimplemented!");
    return false;
}

template<class RawShape>
inline _Box<TPoint<RawShape>> boundingBox(const RawShape& /*sh*/,
                                          const PathTag&)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::boundingBox(shape) unimplemented!");
}

template<class RawShapes>
inline _Box<TPoint<RawShapes>>
boundingBox(const RawShapes& /*sh*/, const MultiPolygonTag&)
{
    static_assert(always_false<RawShapes>::value,
                  "shapelike::boundingBox(shapes) unimplemented!");
}

template<class RawShape>
inline RawShape convexHull(const RawShape& sh, const PathTag&);

template<class RawShapes, class S = typename RawShapes::value_type>
inline S convexHull(const RawShapes& sh, const MultiPolygonTag&);

template<class RawShape>
inline void rotate(RawShape& /*sh*/, const Radians& /*rads*/)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::rotate() unimplemented!");
}

template<class RawShape, class RawPoint>
inline void translate(RawShape& /*sh*/, const RawPoint& /*offs*/)
{
    static_assert(always_false<RawShape>::value,
                  "shapelike::translate() unimplemented!");
}

template<class RawShape>
inline void offset(RawShape& /*sh*/, TCoord<TPoint<RawShape>> /*distance*/)
{
    dout() << "The current geometry backend does not support offsetting!\n";
}

template<class RawShape>
inline std::pair<bool, std::string> isValid(const RawShape& /*sh*/)
{
    return {false, "shapelike::isValid() unimplemented!"};
}

template<class RawPath> inline bool isConvex(const RawPath& sh, const PathTag&)
{
    using Vertex = TPoint<RawPath>;
    auto first = begin(sh);
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

    auto firstprod = zcrossproduct( *(std::prev(std::prev(end(sh)))),
                                    *first,
                                    *middle );

    bool ret = true;
    bool frsign = firstprod > 0;
    while(last != end(sh)) {
        auto &k = *first, &k1 = *middle, &k2 = *last;
        auto zc = zcrossproduct(k, k1, k2);
        ret &= frsign == (zc > 0);
        ++first; ++middle; ++last;
    }

    return ret;
}

// *****************************************************************************
// No need to implement these
// *****************************************************************************

template<class RawShape>
inline typename TContour<RawShape>::iterator
begin(RawShape& sh, const PolygonTag&)
{
    return begin(contour(sh), PathTag());
}

template<class RawShape> // Tag dispatcher
inline auto begin(RawShape& sh) -> decltype(begin(sh, Tag<RawShape>()))
{
    return begin(sh, Tag<RawShape>());
}

template<class RawShape>
inline typename TContour<RawShape>::const_iterator
cbegin(const RawShape& sh, const PolygonTag&)
{
    return cbegin(contour(sh), PathTag());
}

template<class RawShape> // Tag dispatcher
inline auto cbegin(const RawShape& sh) -> decltype(cbegin(sh, Tag<RawShape>()))
{
    return cbegin(sh, Tag<RawShape>());
}

template<class RawShape>
inline typename TContour<RawShape>::iterator
end(RawShape& sh, const PolygonTag&)
{
    return end(contour(sh), PathTag());
}

template<class RawShape> // Tag dispatcher
inline auto end(RawShape& sh) -> decltype(begin(sh, Tag<RawShape>()))
{
    return end(sh, Tag<RawShape>());
}

template<class RawShape>
inline typename TContour<RawShape>::const_iterator
cend(const RawShape& sh, const PolygonTag&)
{
    return cend(contour(sh), PathTag());
}

template<class RawShape> // Tag dispatcher
inline auto cend(const RawShape& sh) -> decltype(cend(sh, Tag<RawShape>()))
{
    return cend(sh, Tag<RawShape>());
}

template<class It> std::reverse_iterator<It> _backward(It iter) {
    return std::reverse_iterator<It>(iter);
}

template<class P> auto rbegin(P& p) -> decltype(_backward(end(p)))
{
    return _backward(end(p));
}

template<class P> auto rcbegin(const P& p) -> decltype(_backward(end(p)))
{
    return _backward(end(p));
}

template<class P> auto rend(P& p) -> decltype(_backward(begin(p)))
{
    return _backward(begin(p));
}

template<class P> auto rcend(const P& p) -> decltype(_backward(cbegin(p)))
{
    return _backward(cbegin(p));
}

template<class P> TPoint<P> front(const P& p) { return *shapelike::cbegin(p); }
template<class P> TPoint<P> back (const P& p) {
    return *backward(shapelike::cend(p));
}

// Optional, does nothing by default
template<class RawShape>
inline void reserve(RawShape& sh, size_t vertex_capacity, const PolygonTag&)
{
    reserve(contour(sh), vertex_capacity, PathTag());
}

template<class T> // Tag dispatcher
inline void reserve(T& sh, size_t vertex_capacity) {
    reserve(sh, vertex_capacity, Tag<T>());
}

template<class RawShape, class...Args>
inline void addVertex(RawShape& sh, const PolygonTag&, Args...args)
{
    addVertex(contour(sh), PathTag(), std::forward<Args>(args)...);
}

template<class RawShape, class...Args> // Tag dispatcher
inline void addVertex(RawShape& sh, Args...args)
{
    addVertex(sh, Tag<RawShape>(), std::forward<Args>(args)...);
}

template<class RawShape>
inline _Box<TPoint<RawShape>> boundingBox(const RawShape& poly, const PolygonTag&)
{
    return boundingBox(contour(poly), PathTag());
}

template<class Box>
inline Box boundingBox(const Box& box, const BoxTag& )
{
    return box;
}

template<class Circle>
inline _Box<typename Circle::PointType> boundingBox(
        const Circle& circ, const CircleTag&)
{
    using Point = typename Circle::PointType;
    using Coord = TCoord<Point>;
    Point pmin = {
        static_cast<Coord>(getX(circ.center()) - circ.radius()),
        static_cast<Coord>(getY(circ.center()) - circ.radius()) };

    Point pmax = {
        static_cast<Coord>(getX(circ.center()) + circ.radius()),
        static_cast<Coord>(getY(circ.center()) + circ.radius()) };

    return {pmin, pmax};
}

template<class S> // Dispatch function
inline _Box<TPoint<S>> boundingBox(const S& sh)
{
    return boundingBox(sh, Tag<S>() );
}

template<class P> _Box<P> boundingBox(const _Box<P>& bb1, const _Box<P>& bb2 )
{
    auto& pminc = bb1.minCorner();
    auto& pmaxc = bb1.maxCorner();
    auto& iminc = bb2.minCorner();
    auto& imaxc = bb2.maxCorner();
    P minc, maxc;
    
    setX(minc, std::min(getX(pminc), getX(iminc)));
    setY(minc, std::min(getY(pminc), getY(iminc)));
    
    setX(maxc, std::max(getX(pmaxc), getX(imaxc)));
    setY(maxc, std::max(getY(pmaxc), getY(imaxc)));
    return _Box<P>(minc, maxc);
}

template<class S1, class S2>
_Box<TPoint<S1>> boundingBox(const S1 &s1, const S2 &s2)
{
    return boundingBox(boundingBox(s1), boundingBox(s2));
}

template<class Box>
inline double area(const Box& box, const BoxTag& )
{
    return box.template area<double>();
}

template<class Circle>
inline double area(const Circle& circ, const CircleTag& )
{
    return circ.area();
}

template<class Cntr, class Unit>
inline Unit area(const Cntr& poly, const PathTag& )
{
    namespace sl = shapelike;
    if (sl::cend(poly) - sl::cbegin(poly) < 3) return 0.0;
  
    Unit a = 0;
    for (auto i = sl::cbegin(poly), j = std::prev(sl::cend(poly)); 
         i < sl::cend(poly); ++i)
    {
        auto xj = Unit(getX(*j)), yj = Unit(getY(*j));
        auto xi = Unit(getX(*i)), yi = Unit(getY(*i));
        a += (xj + xi) *  (yj - yi);
        j = i;
    }
    a /= 2;
    return is_clockwise<Cntr>() ? a : -a;
}

template<class S> inline double area(const S& poly, const PolygonTag& )
{
    auto hls = holes(poly);
    return std::accumulate(hls.begin(), hls.end(), 
                           area(contour(poly), PathTag()),
                           [](double a, const TContour<S> &h){
        return a + area(h, PathTag());    
    });
}

template<class RawShape> // Dispatching function
inline double area(const RawShape& sh)
{
    return area(sh, Tag<RawShape>());
}

template<class RawShapes>
inline double area(const RawShapes& shapes, const MultiPolygonTag&)
{
    using RawShape = typename RawShapes::value_type;
    return std::accumulate(shapes.begin(), shapes.end(), 0.0,
                    [](double a, const RawShape& b) {
        return a += area(b);
    });
}

template<class RawShape>
inline RawShape convexHull(const RawShape& sh, const PolygonTag&)
{
    return create<RawShape>(convexHull(contour(sh), PathTag()));
}

template<class RawShape>
inline auto convexHull(const RawShape& sh)
    -> decltype(convexHull(sh, Tag<RawShape>())) // TODO: C++14 could deduce
{
    return convexHull(sh, Tag<RawShape>());
}

template<class RawShape>
inline RawShape convexHull(const RawShape& sh, const PathTag&)
{
    using Unit = TCompute<RawShape>;
    using Point = TPoint<RawShape>;
    namespace sl = shapelike;
    
    size_t edges = sl::cend(sh) - sl::cbegin(sh);
    if(edges <= 3) return {};
    
    bool closed = false;
    std::vector<Point> U, L;
    U.reserve(1 + edges / 2); L.reserve(1 + edges / 2);
    
    std::vector<Point> pts; pts.reserve(edges);
    std::copy(sl::cbegin(sh), sl::cend(sh), std::back_inserter(pts));
    
    auto fpt = pts.front(), lpt = pts.back();
    if(getX(fpt) == getX(lpt) && getY(fpt) == getY(lpt)) { 
        closed = true; pts.pop_back();
    }
    
    std::sort(pts.begin(), pts.end(), 
              [](const Point& v1, const Point& v2)
    {
        Unit x1 = getX(v1), x2 = getX(v2), y1 = getY(v1), y2 = getY(v2);
        return x1 == x2 ? y1 < y2 : x1 < x2;
    });
    
    auto dir = [](const Point& p, const Point& q, const Point& r) {
        return (Unit(getY(q)) - getY(p)) * (Unit(getX(r)) - getX(p)) -
               (Unit(getX(q)) - getX(p)) * (Unit(getY(r)) - getY(p));
    };
    
    auto ik = pts.begin();
    
    while(ik != pts.end()) {
        
        while(U.size() > 1 && dir(U[U.size() - 2], U.back(), *ik) <= 0) 
            U.pop_back();
        while(L.size() > 1 && dir(L[L.size() - 2], L.back(), *ik) >= 0) 
            L.pop_back();
        
        U.emplace_back(*ik);
        L.emplace_back(*ik);
        
        ++ik;
    }
    
    RawShape ret; reserve(ret, U.size() + L.size());
    if(is_clockwise<RawShape>()) {
        for(auto it = U.begin(); it != std::prev(U.end()); ++it) 
            addVertex(ret, *it);  
        for(auto it = L.rbegin(); it != std::prev(L.rend()); ++it) 
            addVertex(ret, *it);
        if(closed) addVertex(ret, *std::prev(L.rend()));
    } else {
        for(auto it = L.begin(); it != std::prev(L.end()); ++it) 
            addVertex(ret, *it);  
        for(auto it = U.rbegin(); it != std::prev(U.rend()); ++it) 
            addVertex(ret, *it);  
        if(closed) addVertex(ret, *std::prev(U.rend()));
    }
    
    return ret;
}

template<class RawShapes, class S>
inline S convexHull(const RawShapes& sh, const MultiPolygonTag&)
{
    namespace sl = shapelike;
    S cntr;
    for(auto& poly : sh) 
        for(auto it = sl::cbegin(poly); it != sl::cend(poly); ++it) 
            addVertex(cntr, *it);
    
    return convexHull(cntr, Tag<S>());
}

template<class TP, class TC>
inline bool isInside(const TP& point, const TC& circ,
                     const PointTag&, const CircleTag&)
{
    auto r = circ.radius();
    return pointlike::squaredDistance(point, circ.center()) < r * r;
}

template<class TP, class TB>
inline bool isInside(const TP& point, const TB& box,
                     const PointTag&, const BoxTag&)
{
    auto px = getX(point);
    auto py = getY(point);
    auto minx = getX(box.minCorner());
    auto miny = getY(box.minCorner());
    auto maxx = getX(box.maxCorner());
    auto maxy = getY(box.maxCorner());

    return px > minx && px < maxx && py > miny && py < maxy;
}

template<class RawShape, class TC>
inline bool isInside(const RawShape& sh, const TC& circ,
                     const PolygonTag&, const CircleTag&)
{
    return std::all_of(cbegin(sh), cend(sh), [&circ](const TPoint<RawShape>& p)
    {
        return isInside(p, circ, PointTag(), CircleTag());
    });
}

template<class TB, class TC>
inline bool isInside(const TB& box, const TC& circ,
                     const BoxTag&, const CircleTag&)
{
    return isInside(box.minCorner(), circ, PointTag(), CircleTag()) &&
           isInside(box.maxCorner(), circ, PointTag(), CircleTag());
}

template<class TBGuest, class TBHost>
inline bool isInside(const TBGuest& ibb, const TBHost& box,
                     const BoxTag&, const BoxTag&)
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

template<class RawShape, class TB>
inline bool isInside(const RawShape& poly, const TB& box,
                     const PolygonTag&, const BoxTag&)
{
    return isInside(boundingBox(poly), box, BoxTag(), BoxTag());
}

template<class TGuest, class THost>
inline bool isInside(const TGuest& guest, const THost& host) {
    return isInside(guest, host, Tag<TGuest>(), Tag<THost>());
}

template<class RawShape> // Potential O(1) implementation may exist
inline TPoint<RawShape>& vertex(RawShape& sh, unsigned long idx,
                                const PolygonTag&)
{
    return *(shapelike::begin(contour(sh)) + idx);
}

template<class RawShape> // Potential O(1) implementation may exist
inline TPoint<RawShape>& vertex(RawShape& sh, unsigned long idx,
                                const PathTag&)
{
    return *(shapelike::begin(sh) + idx);
}

template<class RawShape> // Potential O(1) implementation may exist
inline TPoint<RawShape>& vertex(RawShape& sh, unsigned long idx)
{
    return vertex(sh, idx, Tag<RawShape>());
}

template<class RawShape> // Potential O(1) implementation may exist
inline const TPoint<RawShape>& vertex(const RawShape& sh,
                                      unsigned long idx,
                                      const PolygonTag&)
{
    return *(shapelike::cbegin(contour(sh)) + idx);
}

template<class RawShape> // Potential O(1) implementation may exist
inline const TPoint<RawShape>& vertex(const RawShape& sh,
                                      unsigned long idx,
                                      const PathTag&)
{
    return *(shapelike::cbegin(sh) + idx);
}


template<class RawShape> // Potential O(1) implementation may exist
inline const TPoint<RawShape>& vertex(const RawShape& sh,
                                      unsigned long idx)
{
    return vertex(sh, idx, Tag<RawShape>());
}

template<class RawShape>
inline size_t contourVertexCount(const RawShape& sh)
{
    return shapelike::cend(sh) - shapelike::cbegin(sh);
}

template<class RawShape, class Fn>
inline void foreachVertex(RawShape& sh, Fn fn, const PolygonTag&) {
    foreachVertex(contour(sh), fn, PathTag());
    for(auto& h : holes(sh)) foreachVertex(h, fn, PathTag());
}

template<class RawShape, class Fn>
inline void foreachVertex(RawShape& sh, Fn fn) {
    foreachVertex(sh, fn, Tag<RawShape>());
}

template<class Poly> inline bool isConvex(const Poly& sh, const PolygonTag&)
{
    bool convex = true;
    convex &= isConvex(contour(sh), PathTag());
    convex &= holeCount(sh) == 0;
    return convex;
}

template<class RawShape> inline bool isConvex(const RawShape& sh) // dispatch
{
    return isConvex(sh, Tag<RawShape>());
}

}

#define DECLARE_MAIN_TYPES(T)        \
    using Polygon = T;               \
    using Point   = TPoint<T>;       \
    using Coord   = TCoord<Point>;   \
    using Contour = TContour<T>;     \
    using Box     = _Box<Point>;     \
    using Circle  = _Circle<Point>;  \
    using Segment = _Segment<Point>; \
    using Polygons = TMultiShape<T>

namespace sl = shapelike;
namespace pl = pointlike;

}

#endif // GEOMETRY_TRAITS_HPP
