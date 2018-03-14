#ifndef slic3r_BoundingBox_hpp_
#define slic3r_BoundingBox_hpp_

#include "libslic3r.h"
#include "Point.hpp"
#include "Polygon.hpp"

namespace Slic3r {

typedef Point   Size;
typedef Point3  Size3;
typedef Pointf  Sizef;
typedef Pointf3 Sizef3;

template <class PointClass>
class BoundingBoxBase
{
public:
    PointClass min;
    PointClass max;
    bool defined;
    
    BoundingBoxBase() : defined(false) {};
    BoundingBoxBase(const PointClass &pmin, const PointClass &pmax) : 
        min(pmin), max(pmax), defined(pmin.x < pmax.x && pmin.y < pmax.y) {}
    BoundingBoxBase(const std::vector<PointClass>& points)
    {
        if (points.empty())
            CONFESS("Empty point set supplied to BoundingBoxBase constructor");

        typename std::vector<PointClass>::const_iterator it = points.begin();
        this->min.x = this->max.x = it->x;
        this->min.y = this->max.y = it->y;
        for (++it; it != points.end(); ++it)
        {
            this->min.x = std::min(it->x, this->min.x);
            this->min.y = std::min(it->y, this->min.y);
            this->max.x = std::max(it->x, this->max.x);
            this->max.y = std::max(it->y, this->max.y);
        }
        this->defined = (this->min.x < this->max.x) && (this->min.y < this->max.y);
    }
    void merge(const PointClass &point);
    void merge(const std::vector<PointClass> &points);
    void merge(const BoundingBoxBase<PointClass> &bb);
    void scale(double factor);
    PointClass size() const;
    double radius() const;
    void translate(coordf_t x, coordf_t y) { assert(this->defined); this->min.translate(x, y); this->max.translate(x, y); }
    void translate(const Pointf &pos) { this->translate(pos.x, pos.y); }
    void offset(coordf_t delta);
    PointClass center() const;
    bool contains(const PointClass &point) const {
        return point.x >= this->min.x && point.x <= this->max.x
            && point.y >= this->min.y && point.y <= this->max.y;
    }
    bool overlap(const BoundingBoxBase<PointClass> &other) const {
        return ! (this->max.x < other.min.x || this->min.x > other.max.x ||
                  this->max.y < other.min.y || this->min.y > other.max.y);
    }
    bool operator==(const BoundingBoxBase<PointClass> &rhs) { return this->min == rhs.min && this->max == rhs.max; }
    bool operator!=(const BoundingBoxBase<PointClass> &rhs) { return ! (*this == rhs); }
};

template <class PointClass>
class BoundingBox3Base : public BoundingBoxBase<PointClass>
{
public:
    BoundingBox3Base() : BoundingBoxBase<PointClass>() {};
    BoundingBox3Base(const PointClass &pmin, const PointClass &pmax) : 
        BoundingBoxBase<PointClass>(pmin, pmax) 
        { if (pmin.z >= pmax.z) BoundingBoxBase<PointClass>::defined = false; }
    BoundingBox3Base(const std::vector<PointClass>& points)
        : BoundingBoxBase<PointClass>(points)
    {
        if (points.empty())
            CONFESS("Empty point set supplied to BoundingBox3Base constructor");

        typename std::vector<PointClass>::const_iterator it = points.begin();
        this->min.z = this->max.z = it->z;
        for (++it; it != points.end(); ++it)
        {
            this->min.z = std::min(it->z, this->min.z);
            this->max.z = std::max(it->z, this->max.z);
        }
        this->defined &= (this->min.z < this->max.z);
    }
    void merge(const PointClass &point);
    void merge(const std::vector<PointClass> &points);
    void merge(const BoundingBox3Base<PointClass> &bb);
    PointClass size() const;
    double radius() const;
    void translate(coordf_t x, coordf_t y, coordf_t z) { this->min.translate(x, y, z); this->max.translate(x, y, z); }
    void translate(const Pointf3 &pos) { this->translate(pos.x, pos.y, pos.z); }
    void offset(coordf_t delta);
    PointClass center() const;

    bool contains(const PointClass &point) const {
        return BoundingBoxBase<PointClass>::contains(point) && point.z >= this->min.z && point.z <= this->max.z;
    }

    bool contains(const BoundingBox3Base<PointClass>& other) const {
        return contains(other.min) && contains(other.max);
    }
};

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
    
    BoundingBox() : BoundingBoxBase<Point>() {};
    BoundingBox(const Point &pmin, const Point &pmax) : BoundingBoxBase<Point>(pmin, pmax) {};
    BoundingBox(const Points &points) : BoundingBoxBase<Point>(points) {};
    BoundingBox(const Lines &lines);

    friend BoundingBox get_extents_rotated(const Points &points, double angle);
};

class BoundingBox3  : public BoundingBox3Base<Point3> 
{
public:
    BoundingBox3() : BoundingBox3Base<Point3>() {};
    BoundingBox3(const Point3 &pmin, const Point3 &pmax) : BoundingBox3Base<Point3>(pmin, pmax) {};
    BoundingBox3(const Points3& points) : BoundingBox3Base<Point3>(points) {};
};

class BoundingBoxf : public BoundingBoxBase<Pointf> 
{
public:
    BoundingBoxf() : BoundingBoxBase<Pointf>() {};
    BoundingBoxf(const Pointf &pmin, const Pointf &pmax) : BoundingBoxBase<Pointf>(pmin, pmax) {};
    BoundingBoxf(const std::vector<Pointf> &points) : BoundingBoxBase<Pointf>(points) {};
};

class BoundingBoxf3 : public BoundingBox3Base<Pointf3> 
{
public:
    BoundingBoxf3() : BoundingBox3Base<Pointf3>() {};
    BoundingBoxf3(const Pointf3 &pmin, const Pointf3 &pmax) : BoundingBox3Base<Pointf3>(pmin, pmax) {};
    BoundingBoxf3(const std::vector<Pointf3> &points) : BoundingBox3Base<Pointf3>(points) {};
};

template<typename VT>
inline bool empty(const BoundingBoxBase<VT> &bb)
{
    return ! bb.defined || bb.min.x >= bb.max.x || bb.min.y >= bb.max.y;
}

template<typename VT>
inline bool empty(const BoundingBox3Base<VT> &bb)
{
    return ! bb.defined || bb.min.x >= bb.max.x || bb.min.y >= bb.max.y || bb.min.z >= bb.max.z;
}

} // namespace Slic3r

#endif
