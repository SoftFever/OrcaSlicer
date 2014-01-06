#include "BoundingBox.hpp"
#include <algorithm>

namespace Slic3r {

template <class PointClass>
BoundingBoxBase<PointClass>::BoundingBoxBase(const std::vector<PointClass> points)
{
    for (typename std::vector<PointClass>::const_iterator it = points.begin(); it != points.end(); ++it) {
        this->min.x = std::min(it->x, this->min.x);
        this->min.y = std::min(it->y, this->min.y);
        this->max.x = std::max(it->x, this->max.x);
        this->max.y = std::max(it->y, this->max.y);
    }
}

template <class PointClass>
BoundingBox3Base<PointClass>::BoundingBox3Base(const std::vector<PointClass> points)
    : BoundingBoxBase<PointClass>(points)
{
    for (typename std::vector<PointClass>::const_iterator it = points.begin(); it != points.end(); ++it) {
        this->min.z = std::min(it->z, this->min.z);
        this->max.z = std::max(it->z, this->max.z);
    }
}

void
BoundingBox::polygon(Polygon* polygon) const
{
    polygon->points.clear();
    polygon->points.resize(4);
    polygon->points[0].x = this->min.x;
    polygon->points[0].y = this->min.y;
    polygon->points[1].x = this->max.x;
    polygon->points[1].y = this->min.y;
    polygon->points[2].x = this->max.x;
    polygon->points[2].y = this->max.y;
    polygon->points[3].x = this->min.x;
    polygon->points[3].y = this->max.y;
}

template <class PointClass> void
BoundingBoxBase<PointClass>::scale(double factor)
{
    this->min.scale(factor);
    this->max.scale(factor);
}

template <class PointClass> void
BoundingBoxBase<PointClass>::merge(const BoundingBoxBase<PointClass> &bb)
{
    this->min.x = std::min(bb.min.x, this->min.x);
    this->min.y = std::min(bb.min.y, this->min.y);
    this->max.x = std::max(bb.max.x, this->max.x);
    this->max.y = std::max(bb.max.y, this->max.y);
}

template <class PointClass> void
BoundingBox3Base<PointClass>::merge(const BoundingBox3Base<PointClass> &bb)
{
    BoundingBoxBase<PointClass>::merge(bb);
    this->min.z = std::min(bb.min.z, this->min.z);
    this->max.z = std::max(bb.max.z, this->max.z);
}

template <class PointClass> PointClass
BoundingBox2Base<PointClass>::size() const
{
    return PointClass(this->max.x - this->min.x, this->max.y - this->min.y);
}

template <class PointClass> PointClass
BoundingBox3Base<PointClass>::size() const
{
    return PointClass(this->max.x - this->min.x, this->max.y - this->min.y, this->max.z - this->min.z);
}

template <class PointClass> void
BoundingBox2Base<PointClass>::translate(coordf_t x, coordf_t y)
{
    this->min.translate(x, y);
    this->max.translate(x, y);
}

template <class PointClass> void
BoundingBox3Base<PointClass>::translate(coordf_t x, coordf_t y, coordf_t z)
{
    this->min.translate(x, y, z);
    this->max.translate(x, y, z);
}

template <class PointClass> PointClass
BoundingBox2Base<PointClass>::center() const
{
    return PointClass(
        (this->max.x - this->min.x)/2,
        (this->max.y - this->min.y)/2
    );
}

template <class PointClass> PointClass
BoundingBox3Base<PointClass>::center() const
{
    return PointClass(
        (this->max.x - this->min.x)/2,
        (this->max.y - this->min.y)/2,
        (this->max.z - this->min.z)/2
    );
}

}
