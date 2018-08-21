#include "BoundingBox.hpp"
#include <algorithm>
#include <assert.h>

#include <Eigen/Dense>

namespace Slic3r {

template BoundingBoxBase<Point>::BoundingBoxBase(const std::vector<Point> &points);
template BoundingBoxBase<Pointf>::BoundingBoxBase(const std::vector<Pointf> &points);

template BoundingBox3Base<Vec3d>::BoundingBox3Base(const std::vector<Vec3d> &points);

BoundingBox::BoundingBox(const Lines &lines)
{
    Points points;
    points.reserve(lines.size());
    for (const Line &line : lines) {
        points.emplace_back(line.a);
        points.emplace_back(line.b);
    }
    *this = BoundingBox(points);
}

void BoundingBox::polygon(Polygon* polygon) const
{
    polygon->points.clear();
    polygon->points.resize(4);
    polygon->points[0](0) = this->min(0);
    polygon->points[0](1) = this->min(1);
    polygon->points[1](0) = this->max(0);
    polygon->points[1](1) = this->min(1);
    polygon->points[2](0) = this->max(0);
    polygon->points[2](1) = this->max(1);
    polygon->points[3](0) = this->min(0);
    polygon->points[3](1) = this->max(1);
}

Polygon BoundingBox::polygon() const
{
    Polygon p;
    this->polygon(&p);
    return p;
}

BoundingBox BoundingBox::rotated(double angle) const
{
    BoundingBox out;
    out.merge(this->min.rotated(angle));
    out.merge(this->max.rotated(angle));
    out.merge(Point(this->min(0), this->max(1)).rotated(angle));
    out.merge(Point(this->max(0), this->min(1)).rotated(angle));
    return out;
}

BoundingBox BoundingBox::rotated(double angle, const Point &center) const
{
    BoundingBox out;
    out.merge(this->min.rotated(angle, center));
    out.merge(this->max.rotated(angle, center));
    out.merge(Point(this->min(0), this->max(1)).rotated(angle, center));
    out.merge(Point(this->max(0), this->min(1)).rotated(angle, center));
    return out;
}

template <class PointClass> void
BoundingBoxBase<PointClass>::scale(double factor)
{
    this->min *= factor;
    this->max *= factor;
}
template void BoundingBoxBase<Point>::scale(double factor);
template void BoundingBoxBase<Pointf>::scale(double factor);
template void BoundingBoxBase<Vec3d>::scale(double factor);

template <class PointClass> void
BoundingBoxBase<PointClass>::merge(const PointClass &point)
{
    if (this->defined) {
        this->min = this->min.cwiseMin(point);
        this->max = this->max.cwiseMax(point);
    } else {
        this->min = point;
        this->max = point;
        this->defined = true;
    }
}
template void BoundingBoxBase<Point>::merge(const Point &point);
template void BoundingBoxBase<Pointf>::merge(const Pointf &point);

template <class PointClass> void
BoundingBoxBase<PointClass>::merge(const std::vector<PointClass> &points)
{
    this->merge(BoundingBoxBase(points));
}
template void BoundingBoxBase<Point>::merge(const Points &points);
template void BoundingBoxBase<Pointf>::merge(const Pointfs &points);

template <class PointClass> void
BoundingBoxBase<PointClass>::merge(const BoundingBoxBase<PointClass> &bb)
{
    assert(bb.defined || bb.min(0) >= bb.max(0) || bb.min(1) >= bb.max(1));
    if (bb.defined) {
        if (this->defined) {
            this->min = this->min.cwiseMin(bb.min);
            this->max = this->max.cwiseMax(bb.max);
        } else {
            this->min = bb.min;
            this->max = bb.max;
            this->defined = true;
        }
    }
}
template void BoundingBoxBase<Point>::merge(const BoundingBoxBase<Point> &bb);
template void BoundingBoxBase<Pointf>::merge(const BoundingBoxBase<Pointf> &bb);

template <class PointClass> void
BoundingBox3Base<PointClass>::merge(const PointClass &point)
{
    if (this->defined) {
        this->min = this->min.cwiseMin(point);
        this->max = this->max.cwiseMax(point);
    } else {
        this->min = point;
        this->max = point;
        this->defined = true;
    }
}
template void BoundingBox3Base<Vec3d>::merge(const Vec3d &point);

template <class PointClass> void
BoundingBox3Base<PointClass>::merge(const std::vector<PointClass> &points)
{
    this->merge(BoundingBox3Base(points));
}
template void BoundingBox3Base<Vec3d>::merge(const Pointf3s &points);

template <class PointClass> void
BoundingBox3Base<PointClass>::merge(const BoundingBox3Base<PointClass> &bb)
{
    assert(bb.defined || bb.min(0) >= bb.max(0) || bb.min(1) >= bb.max(1) || bb.min(2) >= bb.max(2));
    if (bb.defined) {
        if (this->defined) {
            this->min = this->min.cwiseMin(bb.min);
            this->max = this->max.cwiseMax(bb.max);
        } else {
            this->min = bb.min;
            this->max = bb.max;
            this->defined = true;
        }
    }
}
template void BoundingBox3Base<Vec3d>::merge(const BoundingBox3Base<Vec3d> &bb);

template <class PointClass> PointClass
BoundingBoxBase<PointClass>::size() const
{
    return PointClass(this->max(0) - this->min(0), this->max(1) - this->min(1));
}
template Point BoundingBoxBase<Point>::size() const;
template Pointf BoundingBoxBase<Pointf>::size() const;

template <class PointClass> PointClass
BoundingBox3Base<PointClass>::size() const
{
    return PointClass(this->max(0) - this->min(0), this->max(1) - this->min(1), this->max(2) - this->min(2));
}
template Vec3d BoundingBox3Base<Vec3d>::size() const;

template <class PointClass> double BoundingBoxBase<PointClass>::radius() const
{
    assert(this->defined);
    double x = this->max(0) - this->min(0);
    double y = this->max(1) - this->min(1);
    return 0.5 * sqrt(x*x+y*y);
}
template double BoundingBoxBase<Point>::radius() const;
template double BoundingBoxBase<Pointf>::radius() const;

template <class PointClass> double BoundingBox3Base<PointClass>::radius() const
{
    double x = this->max(0) - this->min(0);
    double y = this->max(1) - this->min(1);
    double z = this->max(2) - this->min(2);
    return 0.5 * sqrt(x*x+y*y+z*z);
}
template double BoundingBox3Base<Vec3d>::radius() const;

template <class PointClass> void
BoundingBoxBase<PointClass>::offset(coordf_t delta)
{
    PointClass v(delta, delta);
    this->min -= v;
    this->max += v;
}
template void BoundingBoxBase<Point>::offset(coordf_t delta);
template void BoundingBoxBase<Pointf>::offset(coordf_t delta);

template <class PointClass> void
BoundingBox3Base<PointClass>::offset(coordf_t delta)
{
    PointClass v(delta, delta, delta);
    this->min -= v;
    this->max += v;
}
template void BoundingBox3Base<Vec3d>::offset(coordf_t delta);

template <class PointClass> PointClass
BoundingBoxBase<PointClass>::center() const
{
    return (this->min + this->max) / 2;
}
template Point BoundingBoxBase<Point>::center() const;
template Pointf BoundingBoxBase<Pointf>::center() const;

template <class PointClass> PointClass
BoundingBox3Base<PointClass>::center() const
{
    return (this->min + this->max) / 2;
}
template Vec3d BoundingBox3Base<Vec3d>::center() const;

template <class PointClass> coordf_t
BoundingBox3Base<PointClass>::max_size() const
{
    PointClass s = size();
    return std::max(s(0), std::max(s(1), s(2)));
}
template coordf_t BoundingBox3Base<Vec3d>::max_size() const;

// Align a coordinate to a grid. The coordinate may be negative,
// the aligned value will never be bigger than the original one.
static inline coord_t _align_to_grid(const coord_t coord, const coord_t spacing) {
    // Current C++ standard defines the result of integer division to be rounded to zero,
    // for both positive and negative numbers. Here we want to round down for negative
    // numbers as well.
    coord_t aligned = (coord < 0) ?
            ((coord - spacing + 1) / spacing) * spacing :
            (coord / spacing) * spacing;
    assert(aligned <= coord);
    return aligned;
}

void BoundingBox::align_to_grid(const coord_t cell_size)
{
    if (this->defined) {
        min(0) = _align_to_grid(min(0), cell_size);
        min(1) = _align_to_grid(min(1), cell_size);
    }
}

BoundingBoxf3 BoundingBoxf3::transformed(const Transform3f& matrix) const
{
    Eigen::Matrix<float, 3, 8, Eigen::DontAlign> vertices;

    vertices(0, 0) = (float)min(0); vertices(1, 0) = (float)min(1); vertices(2, 0) = (float)min(2);
    vertices(0, 1) = (float)max(0); vertices(1, 1) = (float)min(1); vertices(2, 1) = (float)min(2);
    vertices(0, 2) = (float)max(0); vertices(1, 2) = (float)max(1); vertices(2, 2) = (float)min(2);
    vertices(0, 3) = (float)min(0); vertices(1, 3) = (float)max(1); vertices(2, 3) = (float)min(2);
    vertices(0, 4) = (float)min(0); vertices(1, 4) = (float)min(1); vertices(2, 4) = (float)max(2);
    vertices(0, 5) = (float)max(0); vertices(1, 5) = (float)min(1); vertices(2, 5) = (float)max(2);
    vertices(0, 6) = (float)max(0); vertices(1, 6) = (float)max(1); vertices(2, 6) = (float)max(2);
    vertices(0, 7) = (float)min(0); vertices(1, 7) = (float)max(1); vertices(2, 7) = (float)max(2);

    Eigen::Matrix<float, 3, 8, Eigen::DontAlign> transf_vertices = matrix * vertices.colwise().homogeneous();

    float min_x = transf_vertices(0, 0);
    float max_x = transf_vertices(0, 0);
    float min_y = transf_vertices(1, 0);
    float max_y = transf_vertices(1, 0);
    float min_z = transf_vertices(2, 0);
    float max_z = transf_vertices(2, 0);

    for (int i = 1; i < 8; ++i)
    {
        min_x = std::min(min_x, transf_vertices(0, i));
        max_x = std::max(max_x, transf_vertices(0, i));
        min_y = std::min(min_y, transf_vertices(1, i));
        max_y = std::max(max_y, transf_vertices(1, i));
        min_z = std::min(min_z, transf_vertices(2, i));
        max_z = std::max(max_z, transf_vertices(2, i));
    }

    return BoundingBoxf3(Vec3d((coordf_t)min_x, (coordf_t)min_y, (coordf_t)min_z), Vec3d((coordf_t)max_x, (coordf_t)max_y, (coordf_t)max_z));
}

}
