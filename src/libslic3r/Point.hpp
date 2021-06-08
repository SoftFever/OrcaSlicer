#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

#include "libslic3r.h"
#include <cstddef>
#include <vector>
#include <cmath>
#include <string>
#include <sstream>
#include <unordered_map>

#include <Eigen/Geometry> 

#include "LocalesUtils.hpp"

namespace Slic3r {

class BoundingBox;
class Line;
class MultiPoint;
class Point;
using Vector = Point;

// Base template for eigen derived vectors
template<int N, int M, class T>
using Mat = Eigen::Matrix<T, N, M, Eigen::DontAlign, N, M>;

template<int N, class T> using Vec = Mat<N, 1, T>;

// Eigen types, to replace the Slic3r's own types in the future.
// Vector types with a fixed point coordinate base type.
using Vec2crd = Eigen::Matrix<coord_t,  2, 1, Eigen::DontAlign>;
using Vec3crd = Eigen::Matrix<coord_t,  3, 1, Eigen::DontAlign>;
using Vec2i   = Eigen::Matrix<int,      2, 1, Eigen::DontAlign>;
using Vec3i   = Eigen::Matrix<int,      3, 1, Eigen::DontAlign>;
using Vec4i   = Eigen::Matrix<int,      4, 1, Eigen::DontAlign>;
using Vec2i32 = Eigen::Matrix<int32_t,  2, 1, Eigen::DontAlign>;
using Vec2i64 = Eigen::Matrix<int64_t,  2, 1, Eigen::DontAlign>;
using Vec3i32 = Eigen::Matrix<int32_t,  3, 1, Eigen::DontAlign>;
using Vec3i64 = Eigen::Matrix<int64_t,  3, 1, Eigen::DontAlign>;

// Vector types with a double coordinate base type.
using Vec2f   = Eigen::Matrix<float,    2, 1, Eigen::DontAlign>;
using Vec3f   = Eigen::Matrix<float,    3, 1, Eigen::DontAlign>;
using Vec2d   = Eigen::Matrix<double,   2, 1, Eigen::DontAlign>;
using Vec3d   = Eigen::Matrix<double,   3, 1, Eigen::DontAlign>;

using Points         = std::vector<Point>;
using PointPtrs      = std::vector<Point*>;
using PointConstPtrs = std::vector<const Point*>;
using Points3        = std::vector<Vec3crd>;
using Pointfs        = std::vector<Vec2d>;
using Vec2ds         = std::vector<Vec2d>;
using Pointf3s       = std::vector<Vec3d>;

using Matrix2f       = Eigen::Matrix<float,  2, 2, Eigen::DontAlign>;
using Matrix2d       = Eigen::Matrix<double, 2, 2, Eigen::DontAlign>;
using Matrix3f       = Eigen::Matrix<float,  3, 3, Eigen::DontAlign>;
using Matrix3d       = Eigen::Matrix<double, 3, 3, Eigen::DontAlign>;
using Matrix4f       = Eigen::Matrix<float,  4, 4, Eigen::DontAlign>;
using Matrix4d       = Eigen::Matrix<double, 4, 4, Eigen::DontAlign>;

template<int N, class T>
using Transform = Eigen::Transform<float, N, Eigen::Affine, Eigen::DontAlign>;

using Transform2f    = Eigen::Transform<float,  2, Eigen::Affine, Eigen::DontAlign>;
using Transform2d    = Eigen::Transform<double, 2, Eigen::Affine, Eigen::DontAlign>;
using Transform3f    = Eigen::Transform<float,  3, Eigen::Affine, Eigen::DontAlign>;
using Transform3d    = Eigen::Transform<double, 3, Eigen::Affine, Eigen::DontAlign>;

// I don't know why Eigen::Transform::Identity() return a const object...
template<int N, class T> Transform<N, T> identity() { return Transform<N, T>::Identity(); }
inline const auto &identity3f = identity<3, float>;
inline const auto &identity3d = identity<3, double>;

inline bool operator<(const Vec2d &lhs, const Vec2d &rhs) { return lhs.x() < rhs.x() || (lhs.x() == rhs.x() && lhs.y() < rhs.y()); }

template<int Options>
int32_t cross2(const Eigen::MatrixBase<Eigen::Matrix<int32_t, 2, 1, Options>> &v1, const Eigen::MatrixBase<Eigen::Matrix<int32_t, 2, 1, Options>> &v2) = delete;

template<typename T, int Options>
inline T cross2(const Eigen::MatrixBase<Eigen::Matrix<T, 2, 1, Options>> &v1, const Eigen::MatrixBase<Eigen::Matrix<T, 2, 1, Options>> &v2)
{
    return v1.x() * v2.y() - v1.y() * v2.x();
}

template<typename Derived, typename Derived2>
inline typename Derived::Scalar cross2(const Eigen::MatrixBase<Derived> &v1, const Eigen::MatrixBase<Derived2> &v2)
{
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value, "cross2(): Scalar types of 1st and 2nd operand must be equal.");
    return v1.x() * v2.y() - v1.y() * v2.x();
}

template<typename T, int Options>
inline Eigen::Matrix<T, 2, 1, Eigen::DontAlign> perp(const Eigen::MatrixBase<Eigen::Matrix<T, 2, 1, Options>> &v) { return Eigen::Matrix<T, 2, 1, Eigen::DontAlign>(- v.y(), v.x()); }

template<class T, int N, int Options>
Eigen::Matrix<T, 2, 1, Eigen::DontAlign> to_2d(const Eigen::MatrixBase<Eigen::Matrix<T, N, 1, Options>> &ptN) { return { ptN.x(), ptN.y() }; }

template<class T, int Options>
Eigen::Matrix<T, 3, 1, Eigen::DontAlign> to_3d(const Eigen::MatrixBase<Eigen::Matrix<T, 2, 1, Options>> & pt, const T z) { return { pt.x(), pt.y(), z }; }

inline Vec2d   unscale(coord_t x, coord_t y) { return Vec2d(unscale<double>(x), unscale<double>(y)); }
inline Vec2d   unscale(const Vec2crd &pt) { return Vec2d(unscale<double>(pt.x()), unscale<double>(pt.y())); }
inline Vec2d   unscale(const Vec2d   &pt) { return Vec2d(unscale<double>(pt.x()), unscale<double>(pt.y())); }
inline Vec3d   unscale(coord_t x, coord_t y, coord_t z) { return Vec3d(unscale<double>(x), unscale<double>(y), unscale<double>(z)); }
inline Vec3d   unscale(const Vec3crd &pt) { return Vec3d(unscale<double>(pt.x()), unscale<double>(pt.y()), unscale<double>(pt.z())); }
inline Vec3d   unscale(const Vec3d   &pt) { return Vec3d(unscale<double>(pt.x()), unscale<double>(pt.y()), unscale<double>(pt.z())); }

inline std::string to_string(const Vec2crd &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + "]"; }
inline std::string to_string(const Vec2d   &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + "]"; }
inline std::string to_string(const Vec3crd &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + ", " + float_to_string_decimal_point(pt.z()) + "]"; }
inline std::string to_string(const Vec3d   &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + ", " + float_to_string_decimal_point(pt.z()) + "]"; }

std::vector<Vec3f> transform(const std::vector<Vec3f>& points, const Transform3f& t);
Pointf3s transform(const Pointf3s& points, const Transform3d& t);

template<int N, class T> using Vec = Eigen::Matrix<T,  N, 1, Eigen::DontAlign, N, 1>;

class Point : public Vec2crd
{
public:
    using coord_type = coord_t;

    Point() : Vec2crd(0, 0) {}
    Point(int32_t x, int32_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int64_t x, int64_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(double x, double y) : Vec2crd(coord_t(lrint(x)), coord_t(lrint(y))) {}
    Point(const Point &rhs) { *this = rhs; }
	explicit Point(const Vec2d& rhs) : Vec2crd(coord_t(lrint(rhs.x())), coord_t(lrint(rhs.y()))) {}
	// This constructor allows you to construct Point from Eigen expressions
    template<typename OtherDerived>
    Point(const Eigen::MatrixBase<OtherDerived> &other) : Vec2crd(other) {}
    static Point new_scale(coordf_t x, coordf_t y) { return Point(coord_t(scale_(x)), coord_t(scale_(y))); }
    static Point new_scale(const Vec2d &v) { return Point(coord_t(scale_(v.x())), coord_t(scale_(v.y()))); }

    // This method allows you to assign Eigen expressions to MyVectorType
    template<typename OtherDerived>
    Point& operator=(const Eigen::MatrixBase<OtherDerived> &other)
    {
        this->Vec2crd::operator=(other);
        return *this;
    }

    Point& operator+=(const Point& rhs) { this->x() += rhs.x(); this->y() += rhs.y(); return *this; }
    Point& operator-=(const Point& rhs) { this->x() -= rhs.x(); this->y() -= rhs.y(); return *this; }
	Point& operator*=(const double &rhs) { this->x() = coord_t(this->x() * rhs); this->y() = coord_t(this->y() * rhs); return *this; }
    Point operator*(const double &rhs) { return Point(this->x() * rhs, this->y() * rhs); }

    void   rotate(double angle) { this->rotate(std::cos(angle), std::sin(angle)); }
    void   rotate(double cos_a, double sin_a) {
        double cur_x = (double)this->x();
        double cur_y = (double)this->y();
        this->x() = (coord_t)round(cos_a * cur_x - sin_a * cur_y);
        this->y() = (coord_t)round(cos_a * cur_y + sin_a * cur_x);
    }

    void   rotate(double angle, const Point &center);
    Point  rotated(double angle) const { Point res(*this); res.rotate(angle); return res; }
    Point  rotated(double cos_a, double sin_a) const { Point res(*this); res.rotate(cos_a, sin_a); return res; }
    Point  rotated(double angle, const Point &center) const { Point res(*this); res.rotate(angle, center); return res; }
    int    nearest_point_index(const Points &points) const;
    int    nearest_point_index(const PointConstPtrs &points) const;
    int    nearest_point_index(const PointPtrs &points) const;
    bool   nearest_point(const Points &points, Point* point) const;
    double ccw(const Point &p1, const Point &p2) const;
    double ccw(const Line &line) const;
    double ccw_angle(const Point &p1, const Point &p2) const;
    Point  projection_onto(const MultiPoint &poly) const;
    Point  projection_onto(const Line &line) const;
};

inline bool operator<(const Point &l, const Point &r) 
{ 
    return l.x() < r.x() || (l.x() == r.x() && l.y() < r.y());
}

inline bool is_approx(const Point &p1, const Point &p2, coord_t epsilon = coord_t(SCALED_EPSILON))
{
	Point d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2f &p1, const Vec2f &p2, float epsilon = float(EPSILON))
{
	Vec2f d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2d &p1, const Vec2d &p2, double epsilon = EPSILON)
{
	Vec2d d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec3f &p1, const Vec3f &p2, float epsilon = float(EPSILON))
{
	Vec3f d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline bool is_approx(const Vec3d &p1, const Vec3d &p2, double epsilon = EPSILON)
{
	Vec3d d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline Point lerp(const Point &a, const Point &b, double t)
{
    assert((t >= -EPSILON) && (t <= 1. + EPSILON));
    return ((1. - t) * a.cast<double>() + t * b.cast<double>()).cast<coord_t>();
}

extern BoundingBox get_extents(const Points &pts);
extern BoundingBox get_extents(const std::vector<Points> &pts);

namespace int128 {
    // Exact orientation predicate,
    // returns +1: CCW, 0: collinear, -1: CW.
    int orient(const Vec2crd &p1, const Vec2crd &p2, const Vec2crd &p3);
    // Exact orientation predicate,
    // returns +1: CCW, 0: collinear, -1: CW.
    int cross(const Vec2crd &v1, const Vec2crd &v2);
}

// To be used by std::unordered_map, std::unordered_multimap and friends.
struct PointHash {
    size_t operator()(const Vec2crd &pt) const {
        return std::hash<coord_t>()(pt.x()) ^ std::hash<coord_t>()(pt.y());
    }
};

// A generic class to search for a closest Point in a given radius.
// It uses std::unordered_multimap to implement an efficient 2D spatial hashing.
// The PointAccessor has to return const Point*.
// If a nullptr is returned, it is ignored by the query.
template<typename ValueType, typename PointAccessor> class ClosestPointInRadiusLookup
{
public:
    ClosestPointInRadiusLookup(coord_t search_radius, PointAccessor point_accessor = PointAccessor()) : 
		m_search_radius(search_radius), m_point_accessor(point_accessor), m_grid_log2(0)
    {
        // Resolution of a grid, twice the search radius + some epsilon.
		coord_t gridres = 2 * m_search_radius + 4;
        m_grid_resolution = gridres;
        assert(m_grid_resolution > 0);
        assert(m_grid_resolution < (coord_t(1) << 30));
		// Compute m_grid_log2 = log2(m_grid_resolution)
		if (m_grid_resolution > 32767) {
			m_grid_resolution >>= 16;
			m_grid_log2 += 16;
		}
		if (m_grid_resolution > 127) {
			m_grid_resolution >>= 8;
			m_grid_log2 += 8;
		}
		if (m_grid_resolution > 7) {
			m_grid_resolution >>= 4;
			m_grid_log2 += 4;
		}
		if (m_grid_resolution > 1) {
			m_grid_resolution >>= 2;
			m_grid_log2 += 2;
		}
		if (m_grid_resolution > 0)
			++ m_grid_log2;
		m_grid_resolution = 1 << m_grid_log2;
		assert(m_grid_resolution >= gridres);
		assert(gridres > m_grid_resolution / 2);
    }

    void insert(const ValueType &value) {
        const Vec2crd *pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x()>>m_grid_log2, pt->y()>>m_grid_log2), value));
    }

    void insert(ValueType &&value) {
        const Vec2crd *pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x()>>m_grid_log2, pt->y()>>m_grid_log2), std::move(value)));
    }

    // Erase a data point equal to value. (ValueType has to declare the operator==).
    // Returns true if the data point equal to value was found and removed.
    bool erase(const ValueType &value) {
        const Point *pt = m_point_accessor(value);
        if (pt != nullptr) {
            // Range of fragment starts around grid_corner, close to pt.
            auto range = m_map.equal_range(Point((*pt).x()>>m_grid_log2, (*pt).y()>>m_grid_log2));
            // Remove the first item.
            for (auto it = range.first; it != range.second; ++ it) {
                if (it->second == value) {
                    m_map.erase(it);
                    return true;
                }
            }
        }
        return false;
    }

    // Return a pair of <ValueType*, distance_squared>
    std::pair<const ValueType*, double> find(const Vec2crd &pt) {
        // Iterate over 4 closest grid cells around pt,
        // find the closest start point inside these cells to pt.
        const ValueType *value_min = nullptr;
        double           dist_min = std::numeric_limits<double>::max();
        // Round pt to a closest grid_cell corner.
        Vec2crd            grid_corner((pt.x()+(m_grid_resolution>>1))>>m_grid_log2, (pt.y()+(m_grid_resolution>>1))>>m_grid_log2);
        // For four neighbors of grid_corner:
        for (coord_t neighbor_y = -1; neighbor_y < 1; ++ neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++ neighbor_x) {
                // Range of fragment starts around grid_corner, close to pt.
                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));
                // Find the map entry closest to pt.
                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType &value = it->second;
                    const Vec2crd *pt2 = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 < dist_min) {
                            dist_min = d2;
                            value_min = &value;
                        }
                    }
                }
            }
        }
        return (value_min != nullptr && dist_min < coordf_t(m_search_radius) * coordf_t(m_search_radius)) ? 
            std::make_pair(value_min, dist_min) : 
            std::make_pair(nullptr, std::numeric_limits<double>::max());
    }

    // Returns all pairs of values and squared distances.
    std::vector<std::pair<const ValueType*, double>> find_all(const Vec2crd &pt) {
        // Iterate over 4 closest grid cells around pt,
        // Round pt to a closest grid_cell corner.
        Vec2crd      grid_corner((pt.x()+(m_grid_resolution>>1))>>m_grid_log2, (pt.y()+(m_grid_resolution>>1))>>m_grid_log2);
        // For four neighbors of grid_corner:
        std::vector<std::pair<const ValueType*, double>> out;
        const double r2 = double(m_search_radius) * m_search_radius;
        for (coord_t neighbor_y = -1; neighbor_y < 1; ++ neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++ neighbor_x) {
                // Range of fragment starts around grid_corner, close to pt.
                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));
                // Find the map entry closest to pt.
                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType &value = it->second;
                    const Vec2crd *pt2 = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 <= r2)
                            out.emplace_back(&value, d2);
                    }
                }
            }
        }
        return out;
    }

private:
    using map_type = typename std::unordered_multimap<Vec2crd, ValueType, PointHash>;
    PointAccessor m_point_accessor;
    map_type m_map;
    coord_t  m_search_radius;
    coord_t  m_grid_resolution;
    coord_t  m_grid_log2;
};

std::ostream& operator<<(std::ostream &stm, const Vec2d &pointf);


// /////////////////////////////////////////////////////////////////////////////
// Type safe conversions to and from scaled and unscaled coordinates
// /////////////////////////////////////////////////////////////////////////////

// Semantics are the following:
// Upscaling (scaled()): only from floating point types (or Vec) to either
//                       floating point or integer 'scaled coord' coordinates.
// Downscaling (unscaled()): from arithmetic (or Vec) to floating point only

// Conversion definition from unscaled to floating point scaled
template<class Tout,
         class Tin,
         class = FloatingOnly<Tin>>
inline constexpr FloatingOnly<Tout> scaled(const Tin &v) noexcept
{
    return Tout(v / Tin(SCALING_FACTOR));
}

// Conversion definition from unscaled to integer 'scaled coord'.
// TODO: is the rounding necessary? Here it is commented  out to show that
// it can be different for integers but it does not have to be. Using
// std::round means loosing noexcept and constexpr modifiers
template<class Tout = coord_t, class Tin, class = FloatingOnly<Tin>>
inline constexpr ScaledCoordOnly<Tout> scaled(const Tin &v) noexcept
{
    //return static_cast<Tout>(std::round(v / SCALING_FACTOR));
    return Tout(v / Tin(SCALING_FACTOR));
}

// Conversion for Eigen vectors (N dimensional points)
template<class Tout = coord_t,
         class Tin,
         int N,
         class = FloatingOnly<Tin>,
         int...EigenArgs>
inline Eigen::Matrix<ArithmeticOnly<Tout>, N, EigenArgs...>
scaled(const Eigen::Matrix<Tin, N, EigenArgs...> &v)
{
    return (v / SCALING_FACTOR).template cast<Tout>();
}

// Conversion from arithmetic scaled type to floating point unscaled
template<class Tout = double,
         class Tin,
         class = ArithmeticOnly<Tin>,
         class = FloatingOnly<Tout>>
inline constexpr Tout unscaled(const Tin &v) noexcept
{
    return Tout(v * Tout(SCALING_FACTOR));
}

// Unscaling for Eigen vectors. Input base type can be arithmetic, output base
// type can only be floating point.
template<class Tout = double,
         class Tin,
         int N,
         class = ArithmeticOnly<Tin>,
         class = FloatingOnly<Tout>,
         int...EigenArgs>
inline constexpr Eigen::Matrix<Tout, N, EigenArgs...>
unscaled(const Eigen::Matrix<Tin, N, EigenArgs...> &v) noexcept
{
    return v.template cast<Tout>() * SCALING_FACTOR;
}

// Align a coordinate to a grid. The coordinate may be negative,
// the aligned value will never be bigger than the original one.
inline coord_t align_to_grid(const coord_t coord, const coord_t spacing) {
    // Current C++ standard defines the result of integer division to be rounded to zero,
    // for both positive and negative numbers. Here we want to round down for negative
    // numbers as well.
    coord_t aligned = (coord < 0) ?
            ((coord - spacing + 1) / spacing) * spacing :
            (coord / spacing) * spacing;
    assert(aligned <= coord);
    return aligned;
}
inline Point   align_to_grid(Point   coord, Point   spacing) 
    { return Point(align_to_grid(coord.x(), spacing.x()), align_to_grid(coord.y(), spacing.y())); }
inline coord_t align_to_grid(coord_t coord, coord_t spacing, coord_t base) 
    { return base + align_to_grid(coord - base, spacing); }
inline Point   align_to_grid(Point   coord, Point   spacing, Point   base)
    { return Point(align_to_grid(coord.x(), spacing.x(), base.x()), align_to_grid(coord.y(), spacing.y(), base.y())); }

} // namespace Slic3r

// start Boost
#include <boost/version.hpp>
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Slic3r::Point> { using type = point_concept; };
   
    template <>
    struct point_traits<Slic3r::Point> {
        using coordinate_type = coord_t;
    
        static inline coordinate_type get(const Slic3r::Point& point, orientation_2d orient) {
            return static_cast<coordinate_type>(point((orient == HORIZONTAL) ? 0 : 1));
        }
    };
    
    template <>
    struct point_mutable_traits<Slic3r::Point> {
        using coordinate_type = coord_t;
        static inline void set(Slic3r::Point& point, orientation_2d orient, coord_t value) {
            point((orient == HORIZONTAL) ? 0 : 1) = value;
        }
        static inline Slic3r::Point construct(coord_t x_value, coord_t y_value) {
            return Slic3r::Point(x_value, y_value);
        }
    };
} }
// end Boost

// Serialization through the Cereal library
namespace cereal {
//	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2crd &v) { archive(v.x(), v.y()); }
//	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3crd &v) { archive(v.x(), v.y(), v.z()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2i   &v) { archive(v.x(), v.y()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3i   &v) { archive(v.x(), v.y(), v.z()); }
//	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2i64 &v) { archive(v.x(), v.y()); }
//	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3i64 &v) { archive(v.x(), v.y(), v.z()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2f   &v) { archive(v.x(), v.y()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3f   &v) { archive(v.x(), v.y(), v.z()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2d   &v) { archive(v.x(), v.y()); }
	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3d   &v) { archive(v.x(), v.y(), v.z()); }

	template<class Archive> void load(Archive& archive, Slic3r::Matrix2f &m) { archive.loadBinary((char*)m.data(), sizeof(float) * 4); }
	template<class Archive> void save(Archive& archive, Slic3r::Matrix2f &m) { archive.saveBinary((char*)m.data(), sizeof(float) * 4); }
}

// To be able to use Vec<> and Mat<> in range based for loops:
namespace Eigen {
template<class T, int N, int M>
T* begin(Slic3r::Mat<N, M, T> &mat) { return mat.data(); }

template<class T, int N, int M>
T* end(Slic3r::Mat<N, M, T> &mat) { return mat.data() + N * M; }

template<class T, int N, int M>
const T* begin(const Slic3r::Mat<N, M, T> &mat) { return mat.data(); }

template<class T, int N, int M>
const T* end(const Slic3r::Mat<N, M, T> &mat) { return mat.data() + N * M; }
} // namespace Eigen

#endif
