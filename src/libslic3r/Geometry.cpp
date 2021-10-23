#include "libslic3r.h"
#include "Exception.hpp"
#include "Geometry.hpp"
#include "ClipperUtils.hpp"
#include "ExPolygon.hpp"
#include "Line.hpp"
#include "clipper.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <utility>
#include <stack>
#include <vector>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/log/trivial.hpp>

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

#include <boost/multiprecision/integer.hpp>

#ifdef SLIC3R_DEBUG
#include "SVG.hpp"
#endif

#ifdef SLIC3R_DEBUG
namespace boost { namespace polygon {

// The following code for the visualization of the boost Voronoi diagram is based on:
//
// Boost.Polygon library voronoi_graphic_utils.hpp header file
//          Copyright Andrii Sydorchuk 2010-2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
template <typename CT>
class voronoi_visual_utils {
 public:
  // Discretize parabolic Voronoi edge.
  // Parabolic Voronoi edges are always formed by one point and one segment
  // from the initial input set.
  //
  // Args:
  //   point: input point.
  //   segment: input segment.
  //   max_dist: maximum discretization distance.
  //   discretization: point discretization of the given Voronoi edge.
  //
  // Template arguments:
  //   InCT: coordinate type of the input geometries (usually integer).
  //   Point: point type, should model point concept.
  //   Segment: segment type, should model segment concept.
  //
  // Important:
  //   discretization should contain both edge endpoints initially.
  template <class InCT1, class InCT2,
            template<class> class Point,
            template<class> class Segment>
  static
  typename enable_if<
    typename gtl_and<
      typename gtl_if<
        typename is_point_concept<
          typename geometry_concept< Point<InCT1> >::type
        >::type
      >::type,
      typename gtl_if<
        typename is_segment_concept<
          typename geometry_concept< Segment<InCT2> >::type
        >::type
      >::type
    >::type,
    void
  >::type discretize(
      const Point<InCT1>& point,
      const Segment<InCT2>& segment,
      const CT max_dist,
      std::vector< Point<CT> >* discretization) {
    // Apply the linear transformation to move start point of the segment to
    // the point with coordinates (0, 0) and the direction of the segment to
    // coincide the positive direction of the x-axis.
    CT segm_vec_x = cast(x(high(segment))) - cast(x(low(segment)));
    CT segm_vec_y = cast(y(high(segment))) - cast(y(low(segment)));
    CT sqr_segment_length = segm_vec_x * segm_vec_x + segm_vec_y * segm_vec_y;

    // Compute x-coordinates of the endpoints of the edge
    // in the transformed space.
    CT projection_start = sqr_segment_length *
        get_point_projection((*discretization)[0], segment);
    CT projection_end = sqr_segment_length *
        get_point_projection((*discretization)[1], segment);

    // Compute parabola parameters in the transformed space.
    // Parabola has next representation:
    // f(x) = ((x-rot_x)^2 + rot_y^2) / (2.0*rot_y).
    CT point_vec_x = cast(x(point)) - cast(x(low(segment)));
    CT point_vec_y = cast(y(point)) - cast(y(low(segment)));
    CT rot_x = segm_vec_x * point_vec_x + segm_vec_y * point_vec_y;
    CT rot_y = segm_vec_x * point_vec_y - segm_vec_y * point_vec_x;

    // Save the last point.
    Point<CT> last_point = (*discretization)[1];
    discretization->pop_back();

    // Use stack to avoid recursion.
    std::stack<CT> point_stack;
    point_stack.push(projection_end);
    CT cur_x = projection_start;
    CT cur_y = parabola_y(cur_x, rot_x, rot_y);

    // Adjust max_dist parameter in the transformed space.
    const CT max_dist_transformed = max_dist * max_dist * sqr_segment_length;
    while (!point_stack.empty()) {
      CT new_x = point_stack.top();
      CT new_y = parabola_y(new_x, rot_x, rot_y);

      // Compute coordinates of the point of the parabola that is
      // furthest from the current line segment.
      CT mid_x = (new_y - cur_y) / (new_x - cur_x) * rot_y + rot_x;
      CT mid_y = parabola_y(mid_x, rot_x, rot_y);

      // Compute maximum distance between the given parabolic arc
      // and line segment that discretize it.
      CT dist = (new_y - cur_y) * (mid_x - cur_x) -
          (new_x - cur_x) * (mid_y - cur_y);
      dist = dist * dist / ((new_y - cur_y) * (new_y - cur_y) +
          (new_x - cur_x) * (new_x - cur_x));
      if (dist <= max_dist_transformed) {
        // Distance between parabola and line segment is less than max_dist.
        point_stack.pop();
        CT inter_x = (segm_vec_x * new_x - segm_vec_y * new_y) /
            sqr_segment_length + cast(x(low(segment)));
        CT inter_y = (segm_vec_x * new_y + segm_vec_y * new_x) /
            sqr_segment_length + cast(y(low(segment)));
        discretization->push_back(Point<CT>(inter_x, inter_y));
        cur_x = new_x;
        cur_y = new_y;
      } else {
        point_stack.push(mid_x);
      }
    }

    // Update last point.
    discretization->back() = last_point;
  }

 private:
  // Compute y(x) = ((x - a) * (x - a) + b * b) / (2 * b).
  static CT parabola_y(CT x, CT a, CT b) {
    return ((x - a) * (x - a) + b * b) / (b + b);
  }

  // Get normalized length of the distance between:
  //   1) point projection onto the segment
  //   2) start point of the segment
  // Return this length divided by the segment length. This is made to avoid
  // sqrt computation during transformation from the initial space to the
  // transformed one and vice versa. The assumption is made that projection of
  // the point lies between the start-point and endpoint of the segment.
  template <class InCT,
            template<class> class Point,
            template<class> class Segment>
  static
  typename enable_if<
    typename gtl_and<
      typename gtl_if<
        typename is_point_concept<
          typename geometry_concept< Point<int> >::type
        >::type
      >::type,
      typename gtl_if<
        typename is_segment_concept<
          typename geometry_concept< Segment<long> >::type
        >::type
      >::type
    >::type,
    CT
  >::type get_point_projection(
      const Point<CT>& point, const Segment<InCT>& segment) {
    CT segment_vec_x = cast(x(high(segment))) - cast(x(low(segment)));
    CT segment_vec_y = cast(y(high(segment))) - cast(y(low(segment)));
    CT point_vec_x = x(point) - cast(x(low(segment)));
    CT point_vec_y = y(point) - cast(y(low(segment)));
    CT sqr_segment_length =
        segment_vec_x * segment_vec_x + segment_vec_y * segment_vec_y;
    CT vec_dot = segment_vec_x * point_vec_x + segment_vec_y * point_vec_y;
    return vec_dot / sqr_segment_length;
  }

  template <typename InCT>
  static CT cast(const InCT& value) {
    return static_cast<CT>(value);
  }
};

} } // namespace boost::polygon
#endif

using namespace boost::polygon;  // provides also high() and low()

namespace Slic3r { namespace Geometry {

// This implementation is based on Andrew's monotone chain 2D convex hull algorithm
Polygon convex_hull(Points pts)
{
    std::sort(pts.begin(), pts.end(), [](const Point& a, const Point& b) { return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const Point& a, const Point& b) { return a.x() == b.x() && a.y() == b.y(); }), pts.end());

    Polygon hull;
    int n = (int)pts.size();
    if (n >= 3) {
        int k = 0;
        hull.points.resize(2 * n);
        // Build lower hull
        for (int i = 0; i < n; ++ i) {
            while (k >= 2 && pts[i].ccw(hull[k-2], hull[k-1]) <= 0)
                -- k;
            hull[k ++] = pts[i];
        }
        // Build upper hull
        for (int i = n-2, t = k+1; i >= 0; i--) {
            while (k >= t && pts[i].ccw(hull[k-2], hull[k-1]) <= 0)
                -- k;
            hull[k ++] = pts[i];
        }
        hull.points.resize(k);
        assert(hull.points.front() == hull.points.back());
        hull.points.pop_back();
    }
    return hull;
}

Pointf3s convex_hull(Pointf3s points)
{
    assert(points.size() >= 3);
    // sort input points
    std::sort(points.begin(), points.end(), [](const Vec3d &a, const Vec3d &b){ return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });

    int n = points.size(), k = 0;
    Pointf3s hull;

    if (n >= 3)
    {
        hull.resize(2 * n);

        // Build lower hull
        for (int i = 0; i < n; ++i)
        {
            Point p = Point::new_scale(points[i](0), points[i](1));
            while (k >= 2)
            {
                Point k1 = Point::new_scale(hull[k - 1](0), hull[k - 1](1));
                Point k2 = Point::new_scale(hull[k - 2](0), hull[k - 2](1));

                if (p.ccw(k2, k1) <= 0)
                    --k;
                else
                    break;
            }

            hull[k++] = points[i];
        }

        // Build upper hull
        for (int i = n - 2, t = k + 1; i >= 0; --i)
        {
            Point p = Point::new_scale(points[i](0), points[i](1));
            while (k >= t)
            {
                Point k1 = Point::new_scale(hull[k - 1](0), hull[k - 1](1));
                Point k2 = Point::new_scale(hull[k - 2](0), hull[k - 2](1));

                if (p.ccw(k2, k1) <= 0)
                    --k;
                else
                    break;
            }

            hull[k++] = points[i];
        }

        hull.resize(k);

        assert(hull.front() == hull.back());
        hull.pop_back();
    }

    return hull;
}

Polygon convex_hull(const Polygons &polygons)
{
    Points pp;
    for (Polygons::const_iterator p = polygons.begin(); p != polygons.end(); ++p) {
        pp.insert(pp.end(), p->points.begin(), p->points.end());
    }
    return convex_hull(std::move(pp));
}

bool directions_parallel(double angle1, double angle2, double max_diff)
{
    double diff = fabs(angle1 - angle2);
    max_diff += EPSILON;
    return diff < max_diff || fabs(diff - PI) < max_diff;
}

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
bool directions_perpendicular(double angle1, double angle2, double max_diff)
{
    double diff = fabs(angle1 - angle2);
    max_diff += EPSILON;
    return fabs(diff - 0.5 * PI) < max_diff || fabs(diff - 1.5 * PI) < max_diff;
}
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

template<class T>
bool contains(const std::vector<T> &vector, const Point &point)
{
    for (typename std::vector<T>::const_iterator it = vector.begin(); it != vector.end(); ++it) {
        if (it->contains(point)) return true;
    }
    return false;
}
template bool contains(const ExPolygons &vector, const Point &point);

double rad2deg_dir(double angle)
{
    angle = (angle < PI) ? (-angle + PI/2.0) : (angle + PI/2.0);
    if (angle < 0) angle += PI;
    return rad2deg(angle);
}

Point circle_center_taubin_newton(const Points::const_iterator& input_begin, const Points::const_iterator& input_end, size_t cycles)
{
    Vec2ds tmp;
    tmp.reserve(std::distance(input_begin, input_end));
    std::transform(input_begin, input_end, std::back_inserter(tmp), [] (const Point& in) { return unscale(in); } );
    Vec2d center = circle_center_taubin_newton(tmp.cbegin(), tmp.end(), cycles);
	return Point::new_scale(center.x(), center.y());
}

/// Adapted from work in "Circular and Linear Regression: Fitting circles and lines by least squares", pg 126
/// Returns a point corresponding to the center of a circle for which all of the points from input_begin to input_end
/// lie on.
Vec2d circle_center_taubin_newton(const Vec2ds::const_iterator& input_begin, const Vec2ds::const_iterator& input_end, size_t cycles)
{
    // calculate the centroid of the data set
    const Vec2d sum = std::accumulate(input_begin, input_end, Vec2d(0,0));
    const size_t n = std::distance(input_begin, input_end);
    const double n_flt = static_cast<double>(n);
    const Vec2d centroid { sum / n_flt };

    // Compute the normalized moments of the data set.
    double Mxx = 0, Myy = 0, Mxy = 0, Mxz = 0, Myz = 0, Mzz = 0;
    for (auto it = input_begin; it < input_end; ++it) {
        // center/normalize the data.
        double Xi {it->x() - centroid.x()};
        double Yi {it->y() - centroid.y()};
        double Zi {Xi*Xi + Yi*Yi};
        Mxy += (Xi*Yi);
        Mxx += (Xi*Xi);
        Myy += (Yi*Yi);
        Mxz += (Xi*Zi);
        Myz += (Yi*Zi);
        Mzz += (Zi*Zi);
    }

    // divide by number of points to get the moments
    Mxx /= n_flt;
    Myy /= n_flt;
    Mxy /= n_flt;
    Mxz /= n_flt;
    Myz /= n_flt;
    Mzz /= n_flt;

    // Compute the coefficients of the characteristic polynomial for the circle
    // eq 5.60
    const double Mz {Mxx + Myy}; // xx + yy = z
    const double Cov_xy {Mxx*Myy - Mxy*Mxy}; // this shows up a couple times so cache it here.
    const double C3 {4.0*Mz};
    const double C2 {-3.0*(Mz*Mz) - Mzz};
    const double C1 {Mz*(Mzz - (Mz*Mz)) + 4.0*Mz*Cov_xy - (Mxz*Mxz) - (Myz*Myz)};
    const double C0 {(Mxz*Mxz)*Myy + (Myz*Myz)*Mxx - 2.0*Mxz*Myz*Mxy - Cov_xy*(Mzz - (Mz*Mz))};

    const double C22 = {C2 + C2};
    const double C33 = {C3 + C3 + C3};

    // solve the characteristic polynomial with Newton's method.
    double xnew = 0.0;
    double ynew = 1e20;

    for (size_t i = 0; i < cycles; ++i) {
        const double yold {ynew};
        ynew = C0 + xnew * (C1 + xnew*(C2 + xnew * C3));
        if (std::abs(ynew) > std::abs(yold)) {
			BOOST_LOG_TRIVIAL(error) << "Geometry: Fit is going in the wrong direction.\n";
            return Vec2d(std::nan(""), std::nan(""));
        }
        const double Dy {C1 + xnew*(C22 + xnew*C33)};

        const double xold {xnew};
        xnew = xold - (ynew / Dy);

        if (std::abs((xnew-xold) / xnew) < 1e-12) i = cycles; // converged, we're done here

        if (xnew < 0) {
            // reset, we went negative
            xnew = 0.0;
        }
    }
    
    // compute the determinant and the circle's parameters now that we've solved.
    double DET = xnew*xnew - xnew*Mz + Cov_xy;

    Vec2d center(Mxz * (Myy - xnew) - Myz * Mxy, Myz * (Mxx - xnew) - Mxz*Mxy);
    center /= (DET * 2.);
    return center + centroid;
}

void simplify_polygons(const Polygons &polygons, double tolerance, Polygons* retval)
{
    Polygons pp;
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++it) {
        Polygon p = *it;
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.push_back(p);
    }
    *retval = Slic3r::simplify_polygons(pp);
}

double linint(double value, double oldmin, double oldmax, double newmin, double newmax)
{
    return (value - oldmin) * (newmax - newmin) / (oldmax - oldmin) + newmin;
}

#if 0
// Point with a weight, by which the points are sorted.
// If the points have the same weight, sort them lexicographically by their positions.
struct ArrangeItem {
    ArrangeItem() {}
    Vec2d    pos;
    coordf_t  weight;
    bool operator<(const ArrangeItem &other) const {
        return weight < other.weight ||
            ((weight == other.weight) && (pos(1) < other.pos(1) || (pos(1) == other.pos(1) && pos(0) < other.pos(0))));
    }
};

Pointfs arrange(size_t num_parts, const Vec2d &part_size, coordf_t gap, const BoundingBoxf* bed_bounding_box)
{
    // Use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm.
    const Vec2d       cell_size(part_size(0) + gap, part_size(1) + gap);

    const BoundingBoxf bed_bbox = (bed_bounding_box != NULL && bed_bounding_box->defined) ? 
        *bed_bounding_box :
        // Bogus bed size, large enough not to trigger the unsufficient bed size error.
        BoundingBoxf(
            Vec2d(0, 0),
            Vec2d(cell_size(0) * num_parts, cell_size(1) * num_parts));

    // This is how many cells we have available into which to put parts.
    size_t cellw = size_t(floor((bed_bbox.size()(0) + gap) / cell_size(0)));
    size_t cellh = size_t(floor((bed_bbox.size()(1) + gap) / cell_size(1)));
    if (num_parts > cellw * cellh)
        throw Slic3r::InvalidArgument("%zu parts won't fit in your print area!\n", num_parts);
    
    // Get a bounding box of cellw x cellh cells, centered at the center of the bed.
    Vec2d       cells_size(cellw * cell_size(0) - gap, cellh * cell_size(1) - gap);
    Vec2d       cells_offset(bed_bbox.center() - 0.5 * cells_size);
    BoundingBoxf cells_bb(cells_offset, cells_size + cells_offset);
    
    // List of cells, sorted by distance from center.
    std::vector<ArrangeItem> cellsorder(cellw * cellh, ArrangeItem());
    for (size_t j = 0; j < cellh; ++ j) {
        // Center of the jth row on the bed.
        coordf_t cy = linint(j + 0.5, 0., double(cellh), cells_bb.min(1), cells_bb.max(1));
        // Offset from the bed center.
        coordf_t yd = cells_bb.center()(1) - cy;
        for (size_t i = 0; i < cellw; ++ i) {
            // Center of the ith column on the bed.
            coordf_t cx = linint(i + 0.5, 0., double(cellw), cells_bb.min(0), cells_bb.max(0));
            // Offset from the bed center.
            coordf_t xd = cells_bb.center()(0) - cx;
            // Cell with a distance from the bed center.
            ArrangeItem &ci = cellsorder[j * cellw + i];
            // Cell center
            ci.pos(0) = cx;
            ci.pos(1) = cy;
            // Square distance of the cell center to the bed center.
            ci.weight = xd * xd + yd * yd;
        }
    }
    // Sort the cells lexicographically by their distances to the bed center and left to right / bttom to top.
    std::sort(cellsorder.begin(), cellsorder.end());
    cellsorder.erase(cellsorder.begin() + num_parts, cellsorder.end());

    // Return the (left,top) corners of the cells.
    Pointfs positions;
    positions.reserve(num_parts);
    for (std::vector<ArrangeItem>::const_iterator it = cellsorder.begin(); it != cellsorder.end(); ++ it)
        positions.push_back(Vec2d(it->pos(0) - 0.5 * part_size(0), it->pos(1) - 0.5 * part_size(1)));
    return positions;
}
#else
class ArrangeItem {
public:
    Vec2d pos = Vec2d::Zero();
    size_t index_x, index_y;
    coordf_t dist;
};
class ArrangeItemIndex {
public:
    coordf_t index;
    ArrangeItem item;
    ArrangeItemIndex(coordf_t _index, ArrangeItem _item) : index(_index), item(_item) {};
};

bool
arrange(size_t total_parts, const Vec2d &part_size, coordf_t dist, const BoundingBoxf* bb, Pointfs &positions)
{
    positions.clear();

    Vec2d part = part_size;

    // use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm
    part(0) += dist;
    part(1) += dist;
    
    Vec2d area(Vec2d::Zero());
    if (bb != NULL && bb->defined) {
        area = bb->size();
    } else {
        // bogus area size, large enough not to trigger the error below
        area(0) = part(0) * total_parts;
        area(1) = part(1) * total_parts;
    }
    
    // this is how many cells we have available into which to put parts
    size_t cellw = floor((area(0) + dist) / part(0));
    size_t cellh = floor((area(1) + dist) / part(1));
    if (total_parts > (cellw * cellh))
        return false;
    
    // total space used by cells
    Vec2d cells(cellw * part(0), cellh * part(1));
    
    // bounding box of total space used by cells
    BoundingBoxf cells_bb;
    cells_bb.merge(Vec2d(0,0)); // min
    cells_bb.merge(cells);  // max
    
    // center bounding box to area
    cells_bb.translate(
        (area(0) - cells(0)) / 2,
        (area(1) - cells(1)) / 2
    );
    
    // list of cells, sorted by distance from center
    std::vector<ArrangeItemIndex> cellsorder;
    
    // work out distance for all cells, sort into list
    for (size_t i = 0; i <= cellw-1; ++i) {
        for (size_t j = 0; j <= cellh-1; ++j) {
            coordf_t cx = linint(i + 0.5, 0, cellw, cells_bb.min(0), cells_bb.max(0));
            coordf_t cy = linint(j + 0.5, 0, cellh, cells_bb.min(1), cells_bb.max(1));
            
            coordf_t xd = fabs((area(0) / 2) - cx);
            coordf_t yd = fabs((area(1) / 2) - cy);
            
            ArrangeItem c;
            c.pos(0) = cx;
            c.pos(1) = cy;
            c.index_x = i;
            c.index_y = j;
            c.dist = xd * xd + yd * yd - fabs((cellw / 2) - (i + 0.5));
            
            // binary insertion sort
            {
                coordf_t index = c.dist;
                size_t low = 0;
                size_t high = cellsorder.size();
                while (low < high) {
                    size_t mid = (low + ((high - low) / 2)) | 0;
                    coordf_t midval = cellsorder[mid].index;
                    
                    if (midval < index) {
                        low = mid + 1;
                    } else if (midval > index) {
                        high = mid;
                    } else {
                        cellsorder.insert(cellsorder.begin() + mid, ArrangeItemIndex(index, c));
                        goto ENDSORT;
                    }
                }
                cellsorder.insert(cellsorder.begin() + low, ArrangeItemIndex(index, c));
            }
            ENDSORT: ;
        }
    }
    
    // the extents of cells actually used by objects
    coordf_t lx = 0;
    coordf_t ty = 0;
    coordf_t rx = 0;
    coordf_t by = 0;

    // now find cells actually used by objects, map out the extents so we can position correctly
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c = cellsorder[i - 1];
        coordf_t cx = c.item.index_x;
        coordf_t cy = c.item.index_y;
        if (i == 1) {
            lx = rx = cx;
            ty = by = cy;
        } else {
            if (cx > rx) rx = cx;
            if (cx < lx) lx = cx;
            if (cy > by) by = cy;
            if (cy < ty) ty = cy;
        }
    }
    // now we actually place objects into cells, positioned such that the left and bottom borders are at 0
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c = cellsorder.front();
        cellsorder.erase(cellsorder.begin());
        coordf_t cx = c.item.index_x - lx;
        coordf_t cy = c.item.index_y - ty;
        
        positions.push_back(Vec2d(cx * part(0), cy * part(1)));
    }
    
    if (bb != NULL && bb->defined) {
        for (Pointfs::iterator p = positions.begin(); p != positions.end(); ++p) {
            p->x() += bb->min(0);
            p->y() += bb->min(1);
        }
    }
    
    return true;
}
#endif

#ifdef SLIC3R_DEBUG
// The following code for the visualization of the boost Voronoi diagram is based on:
//
// Boost.Polygon library voronoi_visualizer.cpp file
//          Copyright Andrii Sydorchuk 2010-2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
namespace Voronoi { namespace Internal {

    typedef double coordinate_type;
    typedef boost::polygon::point_data<coordinate_type> point_type;
    typedef boost::polygon::segment_data<coordinate_type> segment_type;
    typedef boost::polygon::rectangle_data<coordinate_type> rect_type;
    typedef boost::polygon::voronoi_diagram<coordinate_type> VD;
    typedef VD::cell_type cell_type;
    typedef VD::cell_type::source_index_type source_index_type;
    typedef VD::cell_type::source_category_type source_category_type;
    typedef VD::edge_type edge_type;
    typedef VD::cell_container_type cell_container_type;
    typedef VD::cell_container_type vertex_container_type;
    typedef VD::edge_container_type edge_container_type;
    typedef VD::const_cell_iterator const_cell_iterator;
    typedef VD::const_vertex_iterator const_vertex_iterator;
    typedef VD::const_edge_iterator const_edge_iterator;

    static const std::size_t EXTERNAL_COLOR = 1;

    inline void color_exterior(const VD::edge_type* edge) 
    {
        if (edge->color() == EXTERNAL_COLOR)
            return;
        edge->color(EXTERNAL_COLOR);
        edge->twin()->color(EXTERNAL_COLOR);
        const VD::vertex_type* v = edge->vertex1();
        if (v == NULL || !edge->is_primary())
            return;
        v->color(EXTERNAL_COLOR);
        const VD::edge_type* e = v->incident_edge();
        do {
            color_exterior(e);
            e = e->rot_next();
        } while (e != v->incident_edge());
    }

    inline point_type retrieve_point(const std::vector<segment_type> &segments, const cell_type& cell) 
    {
        assert(cell.source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT || cell.source_category() == SOURCE_CATEGORY_SEGMENT_END_POINT);
        return (cell.source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT) ? low(segments[cell.source_index()]) : high(segments[cell.source_index()]);
    }

    inline void clip_infinite_edge(const std::vector<segment_type> &segments, const edge_type& edge, coordinate_type bbox_max_size, std::vector<point_type>* clipped_edge) 
    {
        const cell_type& cell1 = *edge.cell();
        const cell_type& cell2 = *edge.twin()->cell();
        point_type origin, direction;
        // Infinite edges could not be created by two segment sites.
        if (cell1.contains_point() && cell2.contains_point()) {
            point_type p1 = retrieve_point(segments, cell1);
            point_type p2 = retrieve_point(segments, cell2);
            origin.x((p1.x() + p2.x()) * 0.5);
            origin.y((p1.y() + p2.y()) * 0.5);
            direction.x(p1.y() - p2.y());
            direction.y(p2.x() - p1.x());
        } else {
            origin = cell1.contains_segment() ? retrieve_point(segments, cell2) : retrieve_point(segments, cell1);
            segment_type segment = cell1.contains_segment() ? segments[cell1.source_index()] : segments[cell2.source_index()];
            coordinate_type dx = high(segment).x() - low(segment).x();
            coordinate_type dy = high(segment).y() - low(segment).y();
            if ((low(segment) == origin) ^ cell1.contains_point()) {
                direction.x(dy);
                direction.y(-dx);
            } else {
                direction.x(-dy);
                direction.y(dx);
            }
        }
        coordinate_type koef = bbox_max_size / (std::max)(fabs(direction.x()), fabs(direction.y()));
        if (edge.vertex0() == NULL) {
            clipped_edge->push_back(point_type(
                origin.x() - direction.x() * koef,
                origin.y() - direction.y() * koef));
        } else {
            clipped_edge->push_back(
                point_type(edge.vertex0()->x(), edge.vertex0()->y()));
        }
        if (edge.vertex1() == NULL) {
            clipped_edge->push_back(point_type(
                origin.x() + direction.x() * koef,
                origin.y() + direction.y() * koef));
        } else {
            clipped_edge->push_back(
                point_type(edge.vertex1()->x(), edge.vertex1()->y()));
        }
    }

    inline void sample_curved_edge(const std::vector<segment_type> &segments, const edge_type& edge, std::vector<point_type> &sampled_edge, coordinate_type max_dist) 
    {
        point_type point = edge.cell()->contains_point() ?
            retrieve_point(segments, *edge.cell()) :
            retrieve_point(segments, *edge.twin()->cell());
        segment_type segment = edge.cell()->contains_point() ?
            segments[edge.twin()->cell()->source_index()] :
            segments[edge.cell()->source_index()];
        ::boost::polygon::voronoi_visual_utils<coordinate_type>::discretize(point, segment, max_dist, &sampled_edge);
    }

} /* namespace Internal */ } // namespace Voronoi

static inline void dump_voronoi_to_svg(const Lines &lines, /* const */ boost::polygon::voronoi_diagram<double> &vd, const ThickPolylines *polylines, const char *path)
{
    const double        scale                       = 0.2;
    const std::string   inputSegmentPointColor      = "lightseagreen";
    const coord_t       inputSegmentPointRadius     = coord_t(0.09 * scale / SCALING_FACTOR); 
    const std::string   inputSegmentColor           = "lightseagreen";
    const coord_t       inputSegmentLineWidth       = coord_t(0.03 * scale / SCALING_FACTOR);

    const std::string   voronoiPointColor           = "black";
    const coord_t       voronoiPointRadius          = coord_t(0.06 * scale / SCALING_FACTOR);
    const std::string   voronoiLineColorPrimary     = "black";
    const std::string   voronoiLineColorSecondary   = "green";
    const std::string   voronoiArcColor             = "red";
    const coord_t       voronoiLineWidth            = coord_t(0.02 * scale / SCALING_FACTOR);

    const bool          internalEdgesOnly           = false;
    const bool          primaryEdgesOnly            = false;

    BoundingBox bbox = BoundingBox(lines);
    bbox.min(0) -= coord_t(1. / SCALING_FACTOR);
    bbox.min(1) -= coord_t(1. / SCALING_FACTOR);
    bbox.max(0) += coord_t(1. / SCALING_FACTOR);
    bbox.max(1) += coord_t(1. / SCALING_FACTOR);

    ::Slic3r::SVG svg(path, bbox);

    if (polylines != NULL)
        svg.draw(*polylines, "lime", "lime", voronoiLineWidth);

//    bbox.scale(1.2);
    // For clipping of half-lines to some reasonable value.
    // The line will then be clipped by the SVG viewer anyway.
    const double bbox_dim_max = double(bbox.max(0) - bbox.min(0)) + double(bbox.max(1) - bbox.min(1));
    // For the discretization of the Voronoi parabolic segments.
    const double discretization_step = 0.0005 * bbox_dim_max;

    // Make a copy of the input segments with the double type.
    std::vector<Voronoi::Internal::segment_type> segments;
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++ it)
        segments.push_back(Voronoi::Internal::segment_type(
            Voronoi::Internal::point_type(double(it->a(0)), double(it->a(1))), 
            Voronoi::Internal::point_type(double(it->b(0)), double(it->b(1)))));
    
    // Color exterior edges.
    for (boost::polygon::voronoi_diagram<double>::const_edge_iterator it = vd.edges().begin(); it != vd.edges().end(); ++it)
        if (!it->is_finite())
            Voronoi::Internal::color_exterior(&(*it));

    // Draw the end points of the input polygon.
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        svg.draw(it->a, inputSegmentPointColor, inputSegmentPointRadius);
        svg.draw(it->b, inputSegmentPointColor, inputSegmentPointRadius);
    }
    // Draw the input polygon.
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it)
        svg.draw(Line(Point(coord_t(it->a(0)), coord_t(it->a(1))), Point(coord_t(it->b(0)), coord_t(it->b(1)))), inputSegmentColor, inputSegmentLineWidth);

#if 1
    // Draw voronoi vertices.
    for (boost::polygon::voronoi_diagram<double>::const_vertex_iterator it = vd.vertices().begin(); it != vd.vertices().end(); ++it)
        if (! internalEdgesOnly || it->color() != Voronoi::Internal::EXTERNAL_COLOR)
            svg.draw(Point(coord_t(it->x()), coord_t(it->y())), voronoiPointColor, voronoiPointRadius);

    for (boost::polygon::voronoi_diagram<double>::const_edge_iterator it = vd.edges().begin(); it != vd.edges().end(); ++it) {
        if (primaryEdgesOnly && !it->is_primary())
            continue;
        if (internalEdgesOnly && (it->color() == Voronoi::Internal::EXTERNAL_COLOR))
            continue;
        std::vector<Voronoi::Internal::point_type> samples;
        std::string color = voronoiLineColorPrimary;
        if (!it->is_finite()) {
            Voronoi::Internal::clip_infinite_edge(segments, *it, bbox_dim_max, &samples);
            if (! it->is_primary())
                color = voronoiLineColorSecondary;
        } else {
            // Store both points of the segment into samples. sample_curved_edge will split the initial line
            // until the discretization_step is reached.
            samples.push_back(Voronoi::Internal::point_type(it->vertex0()->x(), it->vertex0()->y()));
            samples.push_back(Voronoi::Internal::point_type(it->vertex1()->x(), it->vertex1()->y()));
            if (it->is_curved()) {
                Voronoi::Internal::sample_curved_edge(segments, *it, samples, discretization_step);
                color = voronoiArcColor;
            } else if (! it->is_primary())
                color = voronoiLineColorSecondary;
        }
        for (std::size_t i = 0; i + 1 < samples.size(); ++i)
            svg.draw(Line(Point(coord_t(samples[i].x()), coord_t(samples[i].y())), Point(coord_t(samples[i+1].x()), coord_t(samples[i+1].y()))), color, voronoiLineWidth);
    }
#endif

    if (polylines != NULL)
        svg.draw(*polylines, "blue", voronoiLineWidth);

    svg.Close();
}
#endif /* SLIC3R_DEBUG */

// Euclidian distance of two boost::polygon points.
template<typename T>
T dist(const boost::polygon::point_data<T> &p1,const boost::polygon::point_data<T> &p2)
{
	T dx = p2(0) - p1(0);
	T dy = p2(1) - p1(1);
	return sqrt(dx*dx+dy*dy);
}

// Find a foot point of "px" on a segment "seg".
template<typename segment_type, typename point_type>
inline point_type project_point_to_segment(segment_type &seg, point_type &px)
{
    typedef typename point_type::coordinate_type T;
    const point_type &p0 = low(seg);
    const point_type &p1 = high(seg);
    const point_type  dir(p1(0)-p0(0), p1(1)-p0(1));
    const point_type  dproj(px(0)-p0(0), px(1)-p0(1));
    const T           t = (dir(0)*dproj(0) + dir(1)*dproj(1)) / (dir(0)*dir(0) + dir(1)*dir(1));
    assert(t >= T(-1e-6) && t <= T(1. + 1e-6));
    return point_type(p0(0) + t*dir(0), p0(1) + t*dir(1));
}

template<typename VD, typename SEGMENTS>
inline const typename VD::point_type retrieve_cell_point(const typename VD::cell_type& cell, const SEGMENTS &segments)
{
    assert(cell.source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT || cell.source_category() == SOURCE_CATEGORY_SEGMENT_END_POINT);
    return (cell.source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT) ? low(segments[cell.source_index()]) : high(segments[cell.source_index()]);
}

template<typename VD, typename SEGMENTS>
inline std::pair<typename VD::coord_type, typename VD::coord_type>
measure_edge_thickness(const VD &vd, const typename VD::edge_type& edge, const SEGMENTS &segments)
{
	typedef typename VD::coord_type T;
    const typename VD::point_type  pa(edge.vertex0()->x(), edge.vertex0()->y());
    const typename VD::point_type  pb(edge.vertex1()->x(), edge.vertex1()->y());
    const typename VD::cell_type  &cell1 = *edge.cell();
    const typename VD::cell_type  &cell2 = *edge.twin()->cell();
    if (cell1.contains_segment()) {
        if (cell2.contains_segment()) {
            // Both cells contain a linear segment, the left / right cells are symmetric.
            // Project pa, pb to the left segment.
            const typename VD::segment_type segment1 = segments[cell1.source_index()];
            const typename VD::point_type p1a = project_point_to_segment(segment1, pa);
            const typename VD::point_type p1b = project_point_to_segment(segment1, pb);
            return std::pair<T, T>(T(2.)*dist(pa, p1a), T(2.)*dist(pb, p1b));
        } else {
            // 1st cell contains a linear segment, 2nd cell contains a point.
            // The medial axis between the cells is a parabolic arc.
            // Project pa, pb to the left segment.
            const typename  VD::point_type p2 = retrieve_cell_point<VD>(cell2, segments);
            return std::pair<T, T>(T(2.)*dist(pa, p2), T(2.)*dist(pb, p2));
        }
    } else if (cell2.contains_segment()) {
        // 1st cell contains a point, 2nd cell contains a linear segment.
        // The medial axis between the cells is a parabolic arc.
        const typename VD::point_type p1 = retrieve_cell_point<VD>(cell1, segments);
        return std::pair<T, T>(T(2.)*dist(pa, p1), T(2.)*dist(pb, p1));
    } else {
        // Both cells contain a point. The left / right regions are triangular and symmetric.
        const typename VD::point_type p1 = retrieve_cell_point<VD>(cell1, segments);
        return std::pair<T, T>(T(2.)*dist(pa, p1), T(2.)*dist(pb, p1));
    }
}

// Converts the Line instances of Lines vector to VD::segment_type.
template<typename VD>
class Lines2VDSegments
{
public:
    Lines2VDSegments(const Lines &alines) : lines(alines) {}
    typename VD::segment_type operator[](size_t idx) const {
        return typename VD::segment_type(
            typename VD::point_type(typename VD::coord_type(lines[idx].a(0)), typename VD::coord_type(lines[idx].a(1))),
            typename VD::point_type(typename VD::coord_type(lines[idx].b(0)), typename VD::coord_type(lines[idx].b(1))));
    }
private:
    const Lines &lines;
};

void
MedialAxis::build(ThickPolylines* polylines)
{
    construct_voronoi(this->lines.begin(), this->lines.end(), &this->vd);
    
    /*
    // DEBUG: dump all Voronoi edges
    {
        for (VD::const_edge_iterator edge = this->vd.edges().begin(); edge != this->vd.edges().end(); ++edge) {
            if (edge->is_infinite()) continue;
            
            ThickPolyline polyline;
            polyline.points.push_back(Point( edge->vertex0()->x(), edge->vertex0()->y() ));
            polyline.points.push_back(Point( edge->vertex1()->x(), edge->vertex1()->y() ));
            polylines->push_back(polyline);
        }
        return;
    }
    */
    
    //typedef const VD::vertex_type vert_t;
    typedef const VD::edge_type   edge_t;
    
    // collect valid edges (i.e. prune those not belonging to MAT)
    // note: this keeps twins, so it inserts twice the number of the valid edges
    this->valid_edges.clear();
    {
        std::set<const VD::edge_type*> seen_edges;
        for (VD::const_edge_iterator edge = this->vd.edges().begin(); edge != this->vd.edges().end(); ++edge) {
            // if we only process segments representing closed loops, none if the
            // infinite edges (if any) would be part of our MAT anyway
            if (edge->is_secondary() || edge->is_infinite()) continue;
        
            // don't re-validate twins
            if (seen_edges.find(&*edge) != seen_edges.end()) continue;  // TODO: is this needed?
            seen_edges.insert(&*edge);
            seen_edges.insert(edge->twin());
            
            if (!this->validate_edge(&*edge)) continue;
            this->valid_edges.insert(&*edge);
            this->valid_edges.insert(edge->twin());
        }
    }
    this->edges = this->valid_edges;
    
    // iterate through the valid edges to build polylines
    while (!this->edges.empty()) {
        const edge_t* edge = *this->edges.begin();
        
        // start a polyline
        ThickPolyline polyline;
        polyline.points.push_back(Point( edge->vertex0()->x(), edge->vertex0()->y() ));
        polyline.points.push_back(Point( edge->vertex1()->x(), edge->vertex1()->y() ));
        polyline.width.push_back(this->thickness[edge].first);
        polyline.width.push_back(this->thickness[edge].second);
        
        // remove this edge and its twin from the available edges
        (void)this->edges.erase(edge);
        (void)this->edges.erase(edge->twin());
        
        // get next points
        this->process_edge_neighbors(edge, &polyline);
        
        // get previous points
        {
            ThickPolyline rpolyline;
            this->process_edge_neighbors(edge->twin(), &rpolyline);
            polyline.points.insert(polyline.points.begin(), rpolyline.points.rbegin(), rpolyline.points.rend());
            polyline.width.insert(polyline.width.begin(), rpolyline.width.rbegin(), rpolyline.width.rend());
            polyline.endpoints.first = rpolyline.endpoints.second;
        }
        
        assert(polyline.width.size() == polyline.points.size()*2 - 2);
        
        // prevent loop endpoints from being extended
        if (polyline.first_point() == polyline.last_point()) {
            polyline.endpoints.first = false;
            polyline.endpoints.second = false;
        }
        
        // append polyline to result
        polylines->push_back(polyline);
    }

    #ifdef SLIC3R_DEBUG
    {
        static int iRun = 0;
        dump_voronoi_to_svg(this->lines, this->vd, polylines, debug_out_path("MedialAxis-%d.svg", iRun ++).c_str());
        printf("Thick lines: ");
        for (ThickPolylines::const_iterator it = polylines->begin(); it != polylines->end(); ++ it) {
            ThickLines lines = it->thicklines();
            for (ThickLines::const_iterator it2 = lines.begin(); it2 != lines.end(); ++ it2) {
                printf("%f,%f ", it2->a_width, it2->b_width);
            }
        }
        printf("\n");
    }
    #endif /* SLIC3R_DEBUG */
}

void
MedialAxis::build(Polylines* polylines)
{
    ThickPolylines tp;
    this->build(&tp);
    polylines->insert(polylines->end(), tp.begin(), tp.end());
}

void
MedialAxis::process_edge_neighbors(const VD::edge_type* edge, ThickPolyline* polyline)
{
    while (true) {
        // Since rot_next() works on the edge starting point but we want
        // to find neighbors on the ending point, we just swap edge with
        // its twin.
        const VD::edge_type* twin = edge->twin();
    
        // count neighbors for this edge
        std::vector<const VD::edge_type*> neighbors;
        for (const VD::edge_type* neighbor = twin->rot_next(); neighbor != twin;
            neighbor = neighbor->rot_next()) {
            if (this->valid_edges.count(neighbor) > 0) neighbors.push_back(neighbor);
        }
    
        // if we have a single neighbor then we can continue recursively
        if (neighbors.size() == 1) {
            const VD::edge_type* neighbor = neighbors.front();
            
            // break if this is a closed loop
            if (this->edges.count(neighbor) == 0) return;
            
            Point new_point(neighbor->vertex1()->x(), neighbor->vertex1()->y());
            polyline->points.push_back(new_point);
            polyline->width.push_back(this->thickness[neighbor].first);
            polyline->width.push_back(this->thickness[neighbor].second);
            (void)this->edges.erase(neighbor);
            (void)this->edges.erase(neighbor->twin());
            edge = neighbor;
        } else if (neighbors.size() == 0) {
            polyline->endpoints.second = true;
            return;
        } else {
            // T-shaped or star-shaped joint
            return;
        }
    }
}

bool MedialAxis::validate_edge(const VD::edge_type* edge)
{
    // prevent overflows and detect almost-infinite edges
#ifndef CLIPPERLIB_INT32
    if (std::abs(edge->vertex0()->x()) > double(CLIPPER_MAX_COORD_UNSCALED) || 
        std::abs(edge->vertex0()->y()) > double(CLIPPER_MAX_COORD_UNSCALED) || 
        std::abs(edge->vertex1()->x()) > double(CLIPPER_MAX_COORD_UNSCALED) ||
        std::abs(edge->vertex1()->y()) > double(CLIPPER_MAX_COORD_UNSCALED))
        return false;
#endif // CLIPPERLIB_INT32

    // construct the line representing this edge of the Voronoi diagram
    const Line line(
        Point( edge->vertex0()->x(), edge->vertex0()->y() ),
        Point( edge->vertex1()->x(), edge->vertex1()->y() )
    );
    
    // discard edge if it lies outside the supplied shape
    // this could maybe be optimized (checking inclusion of the endpoints
    // might give false positives as they might belong to the contour itself)
    if (this->expolygon != NULL) {
        if (line.a == line.b) {
            // in this case, contains(line) returns a false positive
            if (!this->expolygon->contains(line.a)) return false;
        } else {
            if (!this->expolygon->contains(line)) return false;
        }
    }
    
    // retrieve the original line segments which generated the edge we're checking
    const VD::cell_type* cell_l = edge->cell();
    const VD::cell_type* cell_r = edge->twin()->cell();
    const Line &segment_l = this->retrieve_segment(cell_l);
    const Line &segment_r = this->retrieve_segment(cell_r);
    
    /*
    SVG svg("edge.svg");
    svg.draw(*this->expolygon);
    svg.draw(line);
    svg.draw(segment_l, "red");
    svg.draw(segment_r, "blue");
    svg.Close();
    */
    
    /*  Calculate thickness of the cross-section at both the endpoints of this edge.
        Our Voronoi edge is part of a CCW sequence going around its Voronoi cell 
        located on the left side. (segment_l).
        This edge's twin goes around segment_r. Thus, segment_r is 
        oriented in the same direction as our main edge, and segment_l is oriented
        in the same direction as our twin edge.
        We used to only consider the (half-)distances to segment_r, and that works
        whenever segment_l and segment_r are almost specular and facing. However, 
        at curves they are staggered and they only face for a very little length
        (our very short edge represents such visibility).
        Both w0 and w1 can be calculated either towards cell_l or cell_r with equal
        results by Voronoi definition.
        When cell_l or cell_r don't refer to the segment but only to an endpoint, we
        calculate the distance to that endpoint instead.  */
    
    coordf_t w0 = cell_r->contains_segment()
        ? segment_r.distance_to(line.a)*2
        : (this->retrieve_endpoint(cell_r) - line.a).cast<double>().norm()*2;
    
    coordf_t w1 = cell_l->contains_segment()
        ? segment_l.distance_to(line.b)*2
        : (this->retrieve_endpoint(cell_l) - line.b).cast<double>().norm()*2;
    
    if (cell_l->contains_segment() && cell_r->contains_segment()) {
        // calculate the relative angle between the two boundary segments
        double angle = fabs(segment_r.orientation() - segment_l.orientation());
        if (angle > PI) angle = 2*PI - angle;
        assert(angle >= 0 && angle <= PI);
        
        // fabs(angle) ranges from 0 (collinear, same direction) to PI (collinear, opposite direction)
        // we're interested only in segments close to the second case (facing segments)
        // so we allow some tolerance.
        // this filter ensures that we're dealing with a narrow/oriented area (longer than thick)
        // we don't run it on edges not generated by two segments (thus generated by one segment
        // and the endpoint of another segment), since their orientation would not be meaningful
        if (PI - angle > PI/8) {
            // angle is not narrow enough
            
            // only apply this filter to segments that are not too short otherwise their 
            // angle could possibly be not meaningful
            if (w0 < SCALED_EPSILON || w1 < SCALED_EPSILON || line.length() >= this->min_width)
                return false;
        }
    } else {
        if (w0 < SCALED_EPSILON || w1 < SCALED_EPSILON)
            return false;
    }
    
    if (w0 < this->min_width && w1 < this->min_width)
        return false;
    
    if (w0 > this->max_width && w1 > this->max_width)
        return false;
    
    this->thickness[edge]         = std::make_pair(w0, w1);
    this->thickness[edge->twin()] = std::make_pair(w1, w0);
    
    return true;
}

const Line& MedialAxis::retrieve_segment(const VD::cell_type* cell) const
{
    return this->lines[cell->source_index()];
}

const Point& MedialAxis::retrieve_endpoint(const VD::cell_type* cell) const
{
    const Line& line = this->retrieve_segment(cell);
    if (cell->source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT) {
        return line.a;
    } else {
        return line.b;
    }
}

void assemble_transform(Transform3d& transform, const Vec3d& translation, const Vec3d& rotation, const Vec3d& scale, const Vec3d& mirror)
{
    transform = Transform3d::Identity();
    transform.translate(translation);
    transform.rotate(Eigen::AngleAxisd(rotation(2), Vec3d::UnitZ()) * Eigen::AngleAxisd(rotation(1), Vec3d::UnitY()) * Eigen::AngleAxisd(rotation(0), Vec3d::UnitX()));
    transform.scale(scale.cwiseProduct(mirror));
}

Transform3d assemble_transform(const Vec3d& translation, const Vec3d& rotation, const Vec3d& scale, const Vec3d& mirror)
{
    Transform3d transform;
    assemble_transform(transform, translation, rotation, scale, mirror);
    return transform;
}

Vec3d extract_euler_angles(const Eigen::Matrix<double, 3, 3, Eigen::DontAlign>& rotation_matrix)
{
    // reference: http://www.gregslabaugh.net/publications/euler.pdf
    Vec3d angles1 = Vec3d::Zero();
    Vec3d angles2 = Vec3d::Zero();
    if (std::abs(std::abs(rotation_matrix(2, 0)) - 1.0) < 1e-5)
    {
        angles1(2) = 0.0;
        if (rotation_matrix(2, 0) < 0.0) // == -1.0
        {
            angles1(1) = 0.5 * (double)PI;
            angles1(0) = angles1(2) + ::atan2(rotation_matrix(0, 1), rotation_matrix(0, 2));
        }
        else // == 1.0
        {
            angles1(1) = - 0.5 * (double)PI;
            angles1(0) = - angles1(2) + ::atan2(- rotation_matrix(0, 1), - rotation_matrix(0, 2));
        }
        angles2 = angles1;
    }
    else
    {
        angles1(1) = -::asin(rotation_matrix(2, 0));
        double inv_cos1 = 1.0 / ::cos(angles1(1));
        angles1(0) = ::atan2(rotation_matrix(2, 1) * inv_cos1, rotation_matrix(2, 2) * inv_cos1);
        angles1(2) = ::atan2(rotation_matrix(1, 0) * inv_cos1, rotation_matrix(0, 0) * inv_cos1);

        angles2(1) = (double)PI - angles1(1);
        double inv_cos2 = 1.0 / ::cos(angles2(1));
        angles2(0) = ::atan2(rotation_matrix(2, 1) * inv_cos2, rotation_matrix(2, 2) * inv_cos2);
        angles2(2) = ::atan2(rotation_matrix(1, 0) * inv_cos2, rotation_matrix(0, 0) * inv_cos2);
    }

    // The following euristic is the best found up to now (in the sense that it works fine with the greatest number of edge use-cases)
    // but there are other use-cases were it does not
    // We need to improve it
    double min_1 = angles1.cwiseAbs().minCoeff();
    double min_2 = angles2.cwiseAbs().minCoeff();
    bool use_1 = (min_1 < min_2) || (is_approx(min_1, min_2) && (angles1.norm() <= angles2.norm()));

    return use_1 ? angles1 : angles2;
}

Vec3d extract_euler_angles(const Transform3d& transform)
{
    // use only the non-translational part of the transform
    Eigen::Matrix<double, 3, 3, Eigen::DontAlign> m = transform.matrix().block(0, 0, 3, 3);
    // remove scale
    m.col(0).normalize();
    m.col(1).normalize();
    m.col(2).normalize();
    return extract_euler_angles(m);
}

Transformation::Flags::Flags()
    : dont_translate(true)
    , dont_rotate(true)
    , dont_scale(true)
    , dont_mirror(true)
{
}

bool Transformation::Flags::needs_update(bool dont_translate, bool dont_rotate, bool dont_scale, bool dont_mirror) const
{
    return (this->dont_translate != dont_translate) || (this->dont_rotate != dont_rotate) || (this->dont_scale != dont_scale) || (this->dont_mirror != dont_mirror);
}

void Transformation::Flags::set(bool dont_translate, bool dont_rotate, bool dont_scale, bool dont_mirror)
{
    this->dont_translate = dont_translate;
    this->dont_rotate = dont_rotate;
    this->dont_scale = dont_scale;
    this->dont_mirror = dont_mirror;
}

Transformation::Transformation()
{
    reset();
}

Transformation::Transformation(const Transform3d& transform)
{
    set_from_transform(transform);
}

void Transformation::set_offset(const Vec3d& offset)
{
    set_offset(X, offset(0));
    set_offset(Y, offset(1));
    set_offset(Z, offset(2));
}

void Transformation::set_offset(Axis axis, double offset)
{
    if (m_offset(axis) != offset)
    {
        m_offset(axis) = offset;
        m_dirty = true;
    }
}

void Transformation::set_rotation(const Vec3d& rotation)
{
    set_rotation(X, rotation(0));
    set_rotation(Y, rotation(1));
    set_rotation(Z, rotation(2));
}

void Transformation::set_rotation(Axis axis, double rotation)
{
    rotation = angle_to_0_2PI(rotation);
    if (is_approx(std::abs(rotation), 2.0 * (double)PI))
        rotation = 0.0;

    if (m_rotation(axis) != rotation)
    {
        m_rotation(axis) = rotation;
        m_dirty = true;
    }
}

void Transformation::set_scaling_factor(const Vec3d& scaling_factor)
{
    set_scaling_factor(X, scaling_factor(0));
    set_scaling_factor(Y, scaling_factor(1));
    set_scaling_factor(Z, scaling_factor(2));
}

void Transformation::set_scaling_factor(Axis axis, double scaling_factor)
{
    if (m_scaling_factor(axis) != std::abs(scaling_factor))
    {
        m_scaling_factor(axis) = std::abs(scaling_factor);
        m_dirty = true;
    }
}

void Transformation::set_mirror(const Vec3d& mirror)
{
    set_mirror(X, mirror(0));
    set_mirror(Y, mirror(1));
    set_mirror(Z, mirror(2));
}

void Transformation::set_mirror(Axis axis, double mirror)
{
    double abs_mirror = std::abs(mirror);
    if (abs_mirror == 0.0)
        mirror = 1.0;
    else if (abs_mirror != 1.0)
        mirror /= abs_mirror;

    if (m_mirror(axis) != mirror)
    {
        m_mirror(axis) = mirror;
        m_dirty = true;
    }
}

void Transformation::set_from_transform(const Transform3d& transform)
{
    // offset
    set_offset(transform.matrix().block(0, 3, 3, 1));

    Eigen::Matrix<double, 3, 3, Eigen::DontAlign> m3x3 = transform.matrix().block(0, 0, 3, 3);

    // mirror
    // it is impossible to reconstruct the original mirroring factors from a matrix,
    // we can only detect if the matrix contains a left handed reference system
    // in which case we reorient it back to right handed by mirroring the x axis
    Vec3d mirror = Vec3d::Ones();
    if (m3x3.col(0).dot(m3x3.col(1).cross(m3x3.col(2))) < 0.0)
    {
        mirror(0) = -1.0;
        // remove mirror
        m3x3.col(0) *= -1.0;
    }
    set_mirror(mirror);

    // scale
    set_scaling_factor(Vec3d(m3x3.col(0).norm(), m3x3.col(1).norm(), m3x3.col(2).norm()));

    // remove scale
    m3x3.col(0).normalize();
    m3x3.col(1).normalize();
    m3x3.col(2).normalize();

    // rotation
    set_rotation(extract_euler_angles(m3x3));

    // forces matrix recalculation matrix
    m_matrix = get_matrix();

//    // debug check
//    if (!m_matrix.isApprox(transform))
//        std::cout << "something went wrong in extracting data from matrix" << std::endl;
}

void Transformation::reset()
{
    m_offset = Vec3d::Zero();
    m_rotation = Vec3d::Zero();
    m_scaling_factor = Vec3d::Ones();
    m_mirror = Vec3d::Ones();
    m_matrix = Transform3d::Identity();
    m_dirty = false;
}

const Transform3d& Transformation::get_matrix(bool dont_translate, bool dont_rotate, bool dont_scale, bool dont_mirror) const
{
    if (m_dirty || m_flags.needs_update(dont_translate, dont_rotate, dont_scale, dont_mirror))
    {
        m_matrix = Geometry::assemble_transform(
            dont_translate ? Vec3d::Zero() : m_offset, 
            dont_rotate ? Vec3d::Zero() : m_rotation,
            dont_scale ? Vec3d::Ones() : m_scaling_factor,
            dont_mirror ? Vec3d::Ones() : m_mirror
            );

        m_flags.set(dont_translate, dont_rotate, dont_scale, dont_mirror);
        m_dirty = false;
    }

    return m_matrix;
}

Transformation Transformation::operator * (const Transformation& other) const
{
    return Transformation(get_matrix() * other.get_matrix());
}

Transformation Transformation::volume_to_bed_transformation(const Transformation& instance_transformation, const BoundingBoxf3& bbox)
{
    Transformation out;

    if (instance_transformation.is_scaling_uniform()) {
        // No need to run the non-linear least squares fitting for uniform scaling.
        // Just set the inverse.
        out.set_from_transform(instance_transformation.get_matrix(true).inverse());
    }
    else if (is_rotation_ninety_degrees(instance_transformation.get_rotation()))
    {
        // Anisotropic scaling, rotation by multiples of ninety degrees.
        Eigen::Matrix3d instance_rotation_trafo =
            (Eigen::AngleAxisd(instance_transformation.get_rotation().z(), Vec3d::UnitZ()) *
            Eigen::AngleAxisd(instance_transformation.get_rotation().y(), Vec3d::UnitY()) *
            Eigen::AngleAxisd(instance_transformation.get_rotation().x(), Vec3d::UnitX())).toRotationMatrix();
        Eigen::Matrix3d volume_rotation_trafo =
            (Eigen::AngleAxisd(-instance_transformation.get_rotation().x(), Vec3d::UnitX()) *
            Eigen::AngleAxisd(-instance_transformation.get_rotation().y(), Vec3d::UnitY()) *
            Eigen::AngleAxisd(-instance_transformation.get_rotation().z(), Vec3d::UnitZ())).toRotationMatrix();

        // 8 corners of the bounding box.
        auto pts = Eigen::MatrixXd(8, 3);
        pts(0, 0) = bbox.min.x(); pts(0, 1) = bbox.min.y(); pts(0, 2) = bbox.min.z();
        pts(1, 0) = bbox.min.x(); pts(1, 1) = bbox.min.y(); pts(1, 2) = bbox.max.z();
        pts(2, 0) = bbox.min.x(); pts(2, 1) = bbox.max.y(); pts(2, 2) = bbox.min.z();
        pts(3, 0) = bbox.min.x(); pts(3, 1) = bbox.max.y(); pts(3, 2) = bbox.max.z();
        pts(4, 0) = bbox.max.x(); pts(4, 1) = bbox.min.y(); pts(4, 2) = bbox.min.z();
        pts(5, 0) = bbox.max.x(); pts(5, 1) = bbox.min.y(); pts(5, 2) = bbox.max.z();
        pts(6, 0) = bbox.max.x(); pts(6, 1) = bbox.max.y(); pts(6, 2) = bbox.min.z();
        pts(7, 0) = bbox.max.x(); pts(7, 1) = bbox.max.y(); pts(7, 2) = bbox.max.z();

        // Corners of the bounding box transformed into the modifier mesh coordinate space, with inverse rotation applied to the modifier.
        auto qs = pts *
            (instance_rotation_trafo *
            Eigen::Scaling(instance_transformation.get_scaling_factor().cwiseProduct(instance_transformation.get_mirror())) *
            volume_rotation_trafo).inverse().transpose();
        // Fill in scaling based on least squares fitting of the bounding box corners.
        Vec3d scale;
        for (int i = 0; i < 3; ++i)
            scale(i) = pts.col(i).dot(qs.col(i)) / pts.col(i).dot(pts.col(i));

        out.set_rotation(Geometry::extract_euler_angles(volume_rotation_trafo));
        out.set_scaling_factor(Vec3d(std::abs(scale(0)), std::abs(scale(1)), std::abs(scale(2))));
        out.set_mirror(Vec3d(scale(0) > 0 ? 1. : -1, scale(1) > 0 ? 1. : -1, scale(2) > 0 ? 1. : -1));
    }
    else
    {
        // General anisotropic scaling, general rotation.
        // Keep the modifier mesh in the instance coordinate system, so the modifier mesh will not be aligned with the world.
        // Scale it to get the required size.
        out.set_scaling_factor(instance_transformation.get_scaling_factor().cwiseInverse());
    }

    return out;
}

// For parsing a transformation matrix from 3MF / AMF.
Transform3d transform3d_from_string(const std::string& transform_str)
{
    assert(is_decimal_separator_point()); // for atof
    Transform3d transform = Transform3d::Identity();

    if (!transform_str.empty())
    {
        std::vector<std::string> mat_elements_str;
        boost::split(mat_elements_str, transform_str, boost::is_any_of(" "), boost::token_compress_on);

        unsigned int size = (unsigned int)mat_elements_str.size();
        if (size == 16)
        {
            unsigned int i = 0;
            for (unsigned int r = 0; r < 4; ++r)
            {
                for (unsigned int c = 0; c < 4; ++c)
                {
                    transform(r, c) = ::atof(mat_elements_str[i++].c_str());
                }
            }
        }
    }

    return transform;
}

Eigen::Quaterniond rotation_xyz_diff(const Vec3d &rot_xyz_from, const Vec3d &rot_xyz_to)
{
    return
        // From the current coordinate system to world.
        Eigen::AngleAxisd(rot_xyz_to(2), Vec3d::UnitZ()) * Eigen::AngleAxisd(rot_xyz_to(1), Vec3d::UnitY()) * Eigen::AngleAxisd(rot_xyz_to(0), Vec3d::UnitX()) *
        // From world to the initial coordinate system.
        Eigen::AngleAxisd(-rot_xyz_from(0), Vec3d::UnitX()) * Eigen::AngleAxisd(-rot_xyz_from(1), Vec3d::UnitY()) * Eigen::AngleAxisd(-rot_xyz_from(2), Vec3d::UnitZ());
}

// This should only be called if it is known, that the two rotations only differ in rotation around the Z axis.
double rotation_diff_z(const Vec3d &rot_xyz_from, const Vec3d &rot_xyz_to)
{
    Eigen::AngleAxisd angle_axis(rotation_xyz_diff(rot_xyz_from, rot_xyz_to));
    Vec3d  axis  = angle_axis.axis();
    double angle = angle_axis.angle();
#ifndef NDEBUG
    if (std::abs(angle) > 1e-8) {
        assert(std::abs(axis.x()) < 1e-8);
        assert(std::abs(axis.y()) < 1e-8);
    }
#endif /* NDEBUG */
    return (axis.z() < 0) ? -angle : angle;
}

namespace rotcalip {

using int256_t = boost::multiprecision::int256_t;
using int128_t = boost::multiprecision::int128_t;

template<class Scalar = int64_t>
inline Scalar magnsq(const Point &p)
{
    return Scalar(p.x()) * p.x() + Scalar(p.y()) * p.y();
}

template<class Scalar = int64_t>
inline Scalar dot(const Point &a, const Point &b)
{
    return Scalar(a.x()) * b.x() + Scalar(a.y()) * b.y();
}

template<class Scalar = int64_t>
inline Scalar dotperp(const Point &a, const Point &b)
{
    return Scalar(a.x()) * b.y() - Scalar(a.y()) * b.x();
}

using boost::multiprecision::abs;

// Compares the angle enclosed by vectors dir and dirA (alpha) with the angle
// enclosed by -dir and dirB (beta). Returns -1 if alpha is less than beta, 0
// if they are equal and 1 if alpha is greater than beta. Note that dir is
// reversed for beta, because it represents the opposite side of a caliper.
int cmp_angles(const Point &dir, const Point &dirA, const Point &dirB) {
    int128_t dotA = dot(dir, dirA);
    int128_t dotB = dot(-dir, dirB);
    int256_t dcosa = int256_t(magnsq(dirB)) * int256_t(abs(dotA)) * dotA;
    int256_t dcosb = int256_t(magnsq(dirA)) * int256_t(abs(dotB)) * dotB;
    int256_t diff = dcosa - dcosb;

    return diff > 0? -1 : (diff < 0 ? 1 : 0);
}

// A helper class to navigate on a polygon. Given a vertex index, one can
// get the edge belonging to that vertex, the coordinates of the vertex, the
// next and previous edges. Stuff that is needed in the rotating calipers algo.
class Idx
{
    size_t m_idx;
    const Polygon *m_poly;
public:
    explicit Idx(const Polygon &p): m_idx{0}, m_poly{&p} {}
    explicit Idx(size_t idx, const Polygon &p): m_idx{idx}, m_poly{&p} {}

    size_t idx() const { return m_idx; }
    void set_idx(size_t i) { m_idx = i; }
    size_t next() const { return (m_idx + 1) % m_poly->size(); }
    size_t inc() { return m_idx = (m_idx + 1) % m_poly->size(); }
    Point prev_dir() const {
        return pt() - (*m_poly)[(m_idx + m_poly->size() - 1) % m_poly->size()];
    }

    const Point &pt() const { return (*m_poly)[m_idx]; }
    const Point dir() const { return (*m_poly)[next()] - pt(); }
    const Point  next_dir() const
    {
        return (*m_poly)[(m_idx + 2) % m_poly->size()] - (*m_poly)[next()];
    }
    const Polygon &poly() const { return *m_poly; }
};

enum class AntipodalVisitMode { Full, EdgesOnly };

// Visit all antipodal pairs starting from the initial ia, ib pair which
// has to be a valid antipodal pair (not checked). fn is called for every
// antipodal pair encountered including the initial one.
// The callback Fn has a signiture of bool(size_t i, size_t j, const Point &dir)
// where i,j are the vertex indices of the antipodal pair and dir is the
// direction of the calipers touching the i vertex.
template<AntipodalVisitMode mode = AntipodalVisitMode::Full, class Fn>
void visit_antipodals (Idx& ia, Idx &ib, Fn &&fn)
{
    // Set current caliper direction to be the lower edge angle from X axis
    int cmp = cmp_angles(ia.prev_dir(), ia.dir(), ib.dir());
    Idx *current = cmp <= 0 ? &ia : &ib, *other = cmp <= 0 ? &ib : &ia;
    Idx *initial = current;
    bool visitor_continue = true;

    size_t start = initial->idx();
    bool finished = false;

    while (visitor_continue && !finished) {
        Point current_dir_a = current == &ia ? current->dir() : -current->dir();
        visitor_continue = fn(ia.idx(), ib.idx(), current_dir_a);

        // Parallel edges encountered. An additional pair of antipodals
        // can be yielded.
        if constexpr (mode == AntipodalVisitMode::Full)
            if (cmp == 0 && visitor_continue) {
                visitor_continue = fn(current == &ia ? ia.idx() : ia.next(),
                                      current == &ib ? ib.idx() : ib.next(),
                                      current_dir_a);
            }

        cmp = cmp_angles(current->dir(), current->next_dir(), other->dir());

        current->inc();
        if (cmp > 0) {
            std::swap(current, other);
        }

        if (initial->idx() == start) finished = true;
    }
}

} // namespace rotcalip

bool convex_polygons_intersect(const Polygon &A, const Polygon &B)
{
    using namespace rotcalip;

    // Establish starting antipodals as extremes in XY plane. Use the
    // easily obtainable bounding boxes to check if A and B is disjoint
    // and return false if the are.
    struct BB
    {
        size_t         xmin = 0, xmax = 0, ymin = 0, ymax = 0;
        const Polygon &P;
        static bool cmpy(const Point &l, const Point &u)
        {
            return l.y() < u.y() || (l.y() == u.y() && l.x() < u.x());
        }

        BB(const Polygon &poly): P{poly}
        {
            for (size_t i = 0; i < P.size(); ++i) {
                if (P[i] < P[xmin]) xmin = i;
                if (P[xmax] < P[i]) xmax = i;
                if (cmpy(P[i], P[ymin])) ymin = i;
                if (cmpy(P[ymax], P[i])) ymax = i;
            }
        }
    };

    BB bA{A}, bB{B};
    BoundingBox bbA{{A[bA.xmin].x(), A[bA.ymin].y()}, {A[bA.xmax].x(), A[bA.ymax].y()}};
    BoundingBox bbB{{B[bB.xmin].x(), B[bB.ymin].y()}, {B[bB.xmax].x(), B[bB.ymax].y()}};

//    if (!bbA.overlap(bbB))
//        return false;

    // Establish starting antipodals as extreme vertex pairs in X or Y direction
    // which reside on different polygons. If no such pair is found, the two
    // polygons are certainly not disjoint.
    Idx imin{bA.xmin, A}, imax{bB.xmax, B};
    if (B[bB.xmin] < imin.pt())  imin = Idx{bB.xmin, B};
    if (imax.pt()  < A[bA.xmax]) imax = Idx{bA.xmax, A};
    if (&imin.poly() == &imax.poly()) {
        imin = Idx{bA.ymin, A};
        imax = Idx{bB.ymax, B};
        if (B[bB.ymin] < imin.pt())  imin = Idx{bB.ymin, B};
        if (imax.pt()  < A[bA.ymax]) imax = Idx{bA.ymax, A};
    }

    if (&imin.poly() == &imax.poly())
        return true;

    bool found_divisor = false;
    visit_antipodals<AntipodalVisitMode::EdgesOnly>(
        imin, imax,
        [&imin, &imax, &found_divisor](size_t ia, size_t ib, const Point &dir) {
            //        std::cout << "A" << ia << " B" << ib << " dir " <<
            //        dir.x() << " " << dir.y() << std::endl;
            const Polygon &A = imin.poly(), &B = imax.poly();

            Point ref_a = A[(ia + 2) % A.size()], ref_b = B[(ib + 2) % B.size()];

            bool is_left_a = dotperp( dir, ref_a - A[ia]) > 0;
            bool is_left_b = dotperp(-dir, ref_b - B[ib]) > 0;

            // If both reference points are on the left (or right) of their
            // respective support lines and the opposite support line is to
            // the right (or left), the divisor line is found. We only test
            // the reference point, as by definition, if that is on one side,
            // all the other points must be on the same side of a support
            // line. If the support lines are collinear, the polygons must be
            // on the same side of their respective support lines.

            auto d = dotperp(dir, B[ib] - A[ia]);
            if (d == 0) {
                // The caliper lines are collinear, not just parallel
                found_divisor = (is_left_a && is_left_b) || (!is_left_a && !is_left_b);
            } else if (d > 0) { // B is to the left of (A, A+1)
                found_divisor = !is_left_a && !is_left_b;
            } else { // B is to the right of (A, A+1)
                found_divisor = is_left_a && is_left_b;
            }

            return !found_divisor;
        });

    // Intersects if the divisor was not found
    return !found_divisor;
}

}} // namespace Slic3r::Geometry
