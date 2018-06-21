#include "BoundingBox.hpp"
#include <algorithm>
#include <assert.h>

#include <Eigen/Dense>

namespace Slic3r {

template BoundingBoxBase<Point>::BoundingBoxBase(const std::vector<Point> &points);
template BoundingBoxBase<Pointf>::BoundingBoxBase(const std::vector<Pointf> &points);

template BoundingBox3Base<Pointf3>::BoundingBox3Base(const std::vector<Pointf3> &points);

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

Polygon
BoundingBox::polygon() const
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
    out.merge(Point(this->min.x, this->max.y).rotated(angle));
    out.merge(Point(this->max.x, this->min.y).rotated(angle));
    return out;
}

BoundingBox BoundingBox::rotated(double angle, const Point &center) const
{
    BoundingBox out;
    out.merge(this->min.rotated(angle, center));
    out.merge(this->max.rotated(angle, center));
    out.merge(Point(this->min.x, this->max.y).rotated(angle, center));
    out.merge(Point(this->max.x, this->min.y).rotated(angle, center));
    return out;
}

template <class PointClass> void
BoundingBoxBase<PointClass>::scale(double factor)
{
    this->min.scale(factor);
    this->max.scale(factor);
}
template void BoundingBoxBase<Point>::scale(double factor);
template void BoundingBoxBase<Pointf>::scale(double factor);
template void BoundingBoxBase<Pointf3>::scale(double factor);

template <class PointClass> void
BoundingBoxBase<PointClass>::merge(const PointClass &point)
{
    if (this->defined) {
        this->min.x = std::min(point.x, this->min.x);
        this->min.y = std::min(point.y, this->min.y);
        this->max.x = std::max(point.x, this->max.x);
        this->max.y = std::max(point.y, this->max.y);
    } else {
        this->min = this->max = point;
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
    assert(bb.defined || bb.min.x >= bb.max.x || bb.min.y >= bb.max.y);
    if (bb.defined) {
        if (this->defined) {
            this->min.x = std::min(bb.min.x, this->min.x);
            this->min.y = std::min(bb.min.y, this->min.y);
            this->max.x = std::max(bb.max.x, this->max.x);
            this->max.y = std::max(bb.max.y, this->max.y);
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
        this->min.z = std::min(point.z, this->min.z);
        this->max.z = std::max(point.z, this->max.z);
    }
    BoundingBoxBase<PointClass>::merge(point);
}
template void BoundingBox3Base<Pointf3>::merge(const Pointf3 &point);

template <class PointClass> void
BoundingBox3Base<PointClass>::merge(const std::vector<PointClass> &points)
{
    this->merge(BoundingBox3Base(points));
}
template void BoundingBox3Base<Pointf3>::merge(const Pointf3s &points);

template <class PointClass> void
BoundingBox3Base<PointClass>::merge(const BoundingBox3Base<PointClass> &bb)
{
    assert(bb.defined || bb.min.x >= bb.max.x || bb.min.y >= bb.max.y || bb.min.z >= bb.max.z);
    if (bb.defined) {
        if (this->defined) {
            this->min.z = std::min(bb.min.z, this->min.z);
            this->max.z = std::max(bb.max.z, this->max.z);
        }
        BoundingBoxBase<PointClass>::merge(bb);
    }
}
template void BoundingBox3Base<Pointf3>::merge(const BoundingBox3Base<Pointf3> &bb);

template <class PointClass> PointClass
BoundingBoxBase<PointClass>::size() const
{
    return PointClass(this->max.x - this->min.x, this->max.y - this->min.y);
}
template Point BoundingBoxBase<Point>::size() const;
template Pointf BoundingBoxBase<Pointf>::size() const;

template <class PointClass> PointClass
BoundingBox3Base<PointClass>::size() const
{
    return PointClass(this->max.x - this->min.x, this->max.y - this->min.y, this->max.z - this->min.z);
}
template Pointf3 BoundingBox3Base<Pointf3>::size() const;

template <class PointClass> double BoundingBoxBase<PointClass>::radius() const
{
    assert(this->defined);
    double x = this->max.x - this->min.x;
    double y = this->max.y - this->min.y;
    return 0.5 * sqrt(x*x+y*y);
}
template double BoundingBoxBase<Point>::radius() const;
template double BoundingBoxBase<Pointf>::radius() const;

template <class PointClass> double BoundingBox3Base<PointClass>::radius() const
{
    double x = this->max.x - this->min.x;
    double y = this->max.y - this->min.y;
    double z = this->max.z - this->min.z;
    return 0.5 * sqrt(x*x+y*y+z*z);
}
template double BoundingBox3Base<Pointf3>::radius() const;

template <class PointClass> void
BoundingBoxBase<PointClass>::offset(coordf_t delta)
{
    this->min.translate(-delta, -delta);
    this->max.translate(delta, delta);
}
template void BoundingBoxBase<Point>::offset(coordf_t delta);
template void BoundingBoxBase<Pointf>::offset(coordf_t delta);

template <class PointClass> void
BoundingBox3Base<PointClass>::offset(coordf_t delta)
{
    this->min.translate(-delta, -delta, -delta);
    this->max.translate(delta, delta, delta);
}
template void BoundingBox3Base<Pointf3>::offset(coordf_t delta);

template <class PointClass> PointClass
BoundingBoxBase<PointClass>::center() const
{
    return PointClass(
        (this->max.x + this->min.x)/2,
        (this->max.y + this->min.y)/2
    );
}
template Point BoundingBoxBase<Point>::center() const;
template Pointf BoundingBoxBase<Pointf>::center() const;

template <class PointClass> PointClass
BoundingBox3Base<PointClass>::center() const
{
    return PointClass(
        (this->max.x + this->min.x)/2,
        (this->max.y + this->min.y)/2,
        (this->max.z + this->min.z)/2
    );
}
template Pointf3 BoundingBox3Base<Pointf3>::center() const;

template <class PointClass> coordf_t
BoundingBox3Base<PointClass>::max_size() const
{
    PointClass s = size();
    return std::max(s.x, std::max(s.y, s.z));
}
template coordf_t BoundingBox3Base<Pointf3>::max_size() const;

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
        min.x = _align_to_grid(min.x, cell_size);
        min.y = _align_to_grid(min.y, cell_size);
    }
}

BoundingBoxf3 BoundingBoxf3::transformed(const std::vector<float>& matrix) const
{
    Eigen::Matrix<float, 3, 8> vertices;

    vertices(0, 0) = (float)min.x; vertices(1, 0) = (float)min.y; vertices(2, 0) = (float)min.z;
    vertices(0, 1) = (float)max.x; vertices(1, 1) = (float)min.y; vertices(2, 1) = (float)min.z;
    vertices(0, 2) = (float)max.x; vertices(1, 2) = (float)max.y; vertices(2, 2) = (float)min.z;
    vertices(0, 3) = (float)min.x; vertices(1, 3) = (float)max.y; vertices(2, 3) = (float)min.z;
    vertices(0, 4) = (float)min.x; vertices(1, 4) = (float)min.y; vertices(2, 4) = (float)max.z;
    vertices(0, 5) = (float)max.x; vertices(1, 5) = (float)min.y; vertices(2, 5) = (float)max.z;
    vertices(0, 6) = (float)max.x; vertices(1, 6) = (float)max.y; vertices(2, 6) = (float)max.z;
    vertices(0, 7) = (float)min.x; vertices(1, 7) = (float)max.y; vertices(2, 7) = (float)max.z;

    Eigen::Transform<float, 3, Eigen::Affine> m;
    ::memcpy((void*)m.data(), (const void*)matrix.data(), 16 * sizeof(float));
    Eigen::Matrix<float, 3, 8> transf_vertices = m * vertices.colwise().homogeneous();

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

    return BoundingBoxf3(Pointf3((coordf_t)min_x, (coordf_t)min_y, (coordf_t)min_z), Pointf3((coordf_t)max_x, (coordf_t)max_y, (coordf_t)max_z));
}

}
