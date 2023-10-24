#ifndef slic3r_BoundingBox_hpp_
#define slic3r_BoundingBox_hpp_

#include "libslic3r.h"
#include "Exception.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include <ostream>

namespace Slic3r {

template <class PointClass>
class BoundingBoxBase
{
public:
    PointClass min;
    PointClass max;
    bool defined;
    
    BoundingBoxBase() : min(PointClass::Zero()), max(PointClass::Zero()), defined(false) {}
    BoundingBoxBase(const PointClass &pmin, const PointClass &pmax) : 
        min(pmin), max(pmax), defined(pmin(0) < pmax(0) && pmin(1) < pmax(1)) {}
    BoundingBoxBase(const PointClass &p1, const PointClass &p2, const PointClass &p3) :
        min(p1), max(p1), defined(false) { merge(p2); merge(p3); }

    template<class It, class = IteratorOnly<It>>
    BoundingBoxBase(It from, It to)
        { construct(*this, from, to); }

    BoundingBoxBase(const std::vector<PointClass> &points)
        : BoundingBoxBase(points.begin(), points.end())
    {}

    void reset() { this->defined = false; this->min = PointClass::Zero(); this->max = PointClass::Zero(); }
    void merge(const PointClass &point);
    void merge(const std::vector<PointClass> &points);
    void merge(const BoundingBoxBase<PointClass> &bb);
    void scale(double factor);
    PointClass size() const;
    double radius() const;
    double area() const { return double(this->max(0) - this->min(0)) * (this->max(1) - this->min(1));    } // BBS
    void translate(coordf_t x, coordf_t y) { assert(this->defined); PointClass v(x, y); this->min += v; this->max += v; }
    void translate(const Vec2d& v0) { PointClass v(v0.x(), v0.y()); this->min += v; this->max += v; }
    void offset(coordf_t delta);
    BoundingBoxBase<PointClass> inflated(coordf_t delta) const throw() { BoundingBoxBase<PointClass> out(*this); out.offset(delta); return out; }
    PointClass center() const;
    bool contains(const PointClass &point) const {
        return point(0) >= this->min(0) && point(0) <= this->max(0)
            && point(1) >= this->min(1) && point(1) <= this->max(1);
    }
    bool contains(const BoundingBoxBase<PointClass> &other) const {
        return contains(other.min) && contains(other.max);
    }
    bool overlap(const BoundingBoxBase<PointClass> &other) const {
        return ! (this->max(0) < other.min(0) || this->min(0) > other.max(0) ||
                  this->max(1) < other.min(1) || this->min(1) > other.max(1));
    }
    PointClass operator[](size_t idx) const {
        switch (idx) {
        case 0:
            return min;
            break;
        case 1:
            return PointClass(max(0), min(1));
            break;
        case 2:
            return max;
            break;
        case 3:
            return PointClass(min(0), max(1));
            break;
        default:
            return PointClass();
            break;
        }
        return PointClass();
    }
    bool operator==(const BoundingBoxBase<PointClass> &rhs) { return this->min == rhs.min && this->max == rhs.max; }
    bool operator!=(const BoundingBoxBase<PointClass> &rhs) { return ! (*this == rhs); }
    friend std::ostream &operator<<(std::ostream &os, const BoundingBoxBase &bbox)
    {
        os << "[" << bbox.max(0) - bbox.min(0) << " x " << bbox.max(1) - bbox.min(1) << "] from (" << bbox.min(0) << ", " << bbox.min(1) << ")";
        return os;
    }
    
private:
    // to access construct()
    friend BoundingBox get_extents<false>(const Points &pts);
    friend BoundingBox get_extents<true>(const Points &pts);

    // if IncludeBoundary, then a bounding box is defined even for a single point.
    // otherwise a bounding box is only defined if it has a positive area.
    // The output bounding box is expected to be set to "undefined" initially.
    template<bool IncludeBoundary = false, class BoundingBoxType, class It, class = IteratorOnly<It>>
    static void construct(BoundingBoxType &out, It from, It to)
    {
        if (from != to) {
            auto it = from;
            out.min = it->template cast<typename PointClass::Scalar>();
            out.max = out.min;
            for (++ it; it != to; ++ it) {
                auto vec = it->template cast<typename PointClass::Scalar>();
                out.min = out.min.cwiseMin(vec);
                out.max = out.max.cwiseMax(vec);
            }
            out.defined = IncludeBoundary || (out.min.x() < out.max.x() && out.min.y() < out.max.y());
        }
    }
};

template <class PointClass>
class BoundingBox3Base : public BoundingBoxBase<PointClass>
{
public:
    BoundingBox3Base() : BoundingBoxBase<PointClass>() {}
    BoundingBox3Base(const PointClass &pmin, const PointClass &pmax) : 
        BoundingBoxBase<PointClass>(pmin, pmax) 
        { if (pmin(2) >= pmax(2)) BoundingBoxBase<PointClass>::defined = false; }
    BoundingBox3Base(const PointClass &p1, const PointClass &p2, const PointClass &p3) :
        BoundingBoxBase<PointClass>(p1, p1) { merge(p2); merge(p3); }

    template<class It, class = IteratorOnly<It> > BoundingBox3Base(It from, It to)
    {
        if (from == to)
            throw Slic3r::InvalidArgument("Empty point set supplied to BoundingBox3Base constructor");

        auto it = from;
        this->min = it->template cast<typename PointClass::Scalar>();
        this->max = this->min;
        for (++ it; it != to; ++ it) {
            auto vec = it->template cast<typename PointClass::Scalar>();
            this->min = this->min.cwiseMin(vec);
            this->max = this->max.cwiseMax(vec);
        }
        this->defined = (this->min(0) < this->max(0)) && (this->min(1) < this->max(1)) && (this->min(2) < this->max(2));
    }

    BoundingBox3Base(const std::vector<PointClass> &points)
        : BoundingBox3Base(points.begin(), points.end())
    {}

    Polygon polygon(bool is_scaled = false) const;//BBS: 2D footprint polygon
    void merge(const PointClass &point);
    void merge(const std::vector<PointClass> &points);
    void merge(const BoundingBox3Base<PointClass> &bb);
    PointClass size() const;
    double radius() const;
    void translate(coordf_t x, coordf_t y, coordf_t z) { assert(this->defined); PointClass v(x, y, z); this->min += v; this->max += v; }
    void translate(const Vec3d &v) { this->min += v; this->max += v; }
    void offset(coordf_t delta);
    BoundingBox3Base<PointClass> inflated(coordf_t delta) const throw() { BoundingBox3Base<PointClass> out(*this); out.offset(delta); return out; }
    PointClass center() const;
    coordf_t max_size() const;

    bool contains(const PointClass &point) const {
        return BoundingBoxBase<PointClass>::contains(point) && point(2) >= this->min(2) && point(2) <= this->max(2);
    }

    bool contains(const BoundingBox3Base<PointClass>& other) const {
        return contains(other.min) && contains(other.max);
    }

    bool intersects(const BoundingBox3Base<PointClass>& other) const {
        return (this->min(0) < other.max(0)) && (this->max(0) > other.min(0)) && (this->min(1) < other.max(1)) && (this->max(1) > other.min(1)) && (this->min(2) < other.max(2)) && (this->max(2) > other.min(2));
    }
};

// Will prevent warnings caused by non existing definition of template in hpp
extern template void     BoundingBoxBase<Point>::scale(double factor);
extern template void     BoundingBoxBase<Vec2d>::scale(double factor);
extern template void     BoundingBoxBase<Vec3d>::scale(double factor);
extern template void     BoundingBoxBase<Point>::offset(coordf_t delta);
extern template void     BoundingBoxBase<Vec2d>::offset(coordf_t delta);
extern template void     BoundingBoxBase<Point>::merge(const Point &point);
extern template void     BoundingBoxBase<Vec2f>::merge(const Vec2f &point);
extern template void     BoundingBoxBase<Vec2d>::merge(const Vec2d &point);
extern template void     BoundingBoxBase<Point>::merge(const Points &points);
extern template void     BoundingBoxBase<Vec2d>::merge(const Pointfs &points);
extern template void     BoundingBoxBase<Point>::merge(const BoundingBoxBase<Point> &bb);
extern template void     BoundingBoxBase<Vec2f>::merge(const BoundingBoxBase<Vec2f> &bb);
extern template void     BoundingBoxBase<Vec2d>::merge(const BoundingBoxBase<Vec2d> &bb);
extern template Point    BoundingBoxBase<Point>::size() const;
extern template Vec2f    BoundingBoxBase<Vec2f>::size() const;
extern template Vec2d    BoundingBoxBase<Vec2d>::size() const;
extern template double   BoundingBoxBase<Point>::radius() const;
extern template double   BoundingBoxBase<Vec2d>::radius() const;
extern template Point    BoundingBoxBase<Point>::center() const;
extern template Vec2f    BoundingBoxBase<Vec2f>::center() const;
extern template Vec2d    BoundingBoxBase<Vec2d>::center() const;
extern template void     BoundingBox3Base<Vec3f>::merge(const Vec3f &point);
extern template void     BoundingBox3Base<Vec3d>::merge(const Vec3d &point);
extern template void     BoundingBox3Base<Vec3d>::merge(const Pointf3s &points);
extern template void     BoundingBox3Base<Vec3d>::merge(const BoundingBox3Base<Vec3d> &bb);
extern template Vec3f    BoundingBox3Base<Vec3f>::size() const;
extern template Vec3d    BoundingBox3Base<Vec3d>::size() const;
extern template double   BoundingBox3Base<Vec3d>::radius() const;
extern template void     BoundingBox3Base<Vec3d>::offset(coordf_t delta);
extern template Vec3f    BoundingBox3Base<Vec3f>::center() const;
extern template Vec3d    BoundingBox3Base<Vec3d>::center() const;
extern template coordf_t BoundingBox3Base<Vec3f>::max_size() const;
extern template coordf_t BoundingBox3Base<Vec3d>::max_size() const;

class BoundingBox : public BoundingBoxBase<Point>
{
public:
    void polygon(Polygon* polygon) const;
    Polygon polygon() const;
    BoundingBox rotated(double angle) const;
    BoundingBox rotated(double angle, const Point &center) const;
    void rotate(double angle) { (*this) = this->rotated(angle); }
    void rotate(double angle, const Point &center) { (*this) = this->rotated(angle, center); }
    // Align the min corner to a grid of cell_size x cell_size cells,
    // to encompass the original bounding box.
    void align_to_grid(const coord_t cell_size);
    
    BoundingBox() : BoundingBoxBase<Point>() {}
    BoundingBox(const Point &pmin, const Point &pmax) : BoundingBoxBase<Point>(pmin, pmax) {}
    BoundingBox(const Points &points) : BoundingBoxBase<Point>(points) {}

    BoundingBox inflated(coordf_t delta) const throw() { BoundingBox out(*this); out.offset(delta); return out; }

    friend BoundingBox get_extents_rotated(const Points &points, double angle);
};

class BoundingBox3  : public BoundingBox3Base<Vec3crd> 
{
public:
    BoundingBox3() : BoundingBox3Base<Vec3crd>() {}
    BoundingBox3(const Vec3crd &pmin, const Vec3crd &pmax) : BoundingBox3Base<Vec3crd>(pmin, pmax) {}
    BoundingBox3(const Points3& points) : BoundingBox3Base<Vec3crd>(points) {}
};

class BoundingBoxf : public BoundingBoxBase<Vec2d> 
{
public:
    BoundingBoxf() : BoundingBoxBase<Vec2d>() {}
    BoundingBoxf(const Vec2d &pmin, const Vec2d &pmax) : BoundingBoxBase<Vec2d>(pmin, pmax) {}
    BoundingBoxf(const std::vector<Vec2d> &points) : BoundingBoxBase<Vec2d>(points) {}
};

class BoundingBoxf3 : public BoundingBox3Base<Vec3d> 
{
public:
    using BoundingBox3Base::BoundingBox3Base;

    BoundingBoxf3 transformed(const Transform3d& matrix) const;
};

template<typename VT>
inline bool empty(const BoundingBoxBase<VT> &bb)
{
    return ! bb.defined || bb.min(0) >= bb.max(0) || bb.min(1) >= bb.max(1);
}

template<typename VT>
inline bool empty(const BoundingBox3Base<VT> &bb)
{
    return ! bb.defined || bb.min(0) >= bb.max(0) || bb.min(1) >= bb.max(1) || bb.min(2) >= bb.max(2);
}

inline BoundingBox scaled(const BoundingBoxf &bb) { return {scaled(bb.min), scaled(bb.max)}; }
inline BoundingBox3 scaled(const BoundingBoxf3 &bb) { return {scaled(bb.min), scaled(bb.max)}; }
inline BoundingBoxf unscaled(const BoundingBox &bb) { return {unscaled(bb.min), unscaled(bb.max)}; }
inline BoundingBoxf3 unscaled(const BoundingBox3 &bb) { return {unscaled(bb.min), unscaled(bb.max)}; }

template<class Tout, class Tin>
auto cast(const BoundingBoxBase<Tin> &b)
{
    return BoundingBoxBase<Vec<3, Tout>>{b.min.template cast<Tout>(),
                                         b.max.template cast<Tout>()};
}

template<class Tout, class Tin>
auto cast(const BoundingBox3Base<Tin> &b)
{
    return BoundingBox3Base<Vec<3, Tout>>{b.min.template cast<Tout>(),
                                          b.max.template cast<Tout>()};
}

} // namespace Slic3r

// Serialization through the Cereal library
namespace cereal {
	template<class Archive> void serialize(Archive& archive, Slic3r::BoundingBox   &bb) { archive(bb.min, bb.max, bb.defined); }
	template<class Archive> void serialize(Archive& archive, Slic3r::BoundingBox3  &bb) { archive(bb.min, bb.max, bb.defined); }
	template<class Archive> void serialize(Archive& archive, Slic3r::BoundingBoxf  &bb) { archive(bb.min, bb.max, bb.defined); }
	template<class Archive> void serialize(Archive& archive, Slic3r::BoundingBoxf3 &bb) { archive(bb.min, bb.max, bb.defined); }
}

#endif
