#include "Point.hpp"
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "Int128.hpp"
#include <algorithm>
#include <cmath>

namespace Slic3r {

Point::Point(double x, double y)
{
    this->x = lrint(x);
    this->y = lrint(y);
}

std::string
Point::wkt() const
{
    std::ostringstream ss;
    ss << "POINT(" << this->x << " " << this->y << ")";
    return ss.str();
}

std::string
Point::dump_perl() const
{
    std::ostringstream ss;
    ss << "[" << this->x << "," << this->y << "]";
    return ss.str();
}

void
Point::scale(double factor)
{
    this->x *= factor;
    this->y *= factor;
}

void
Point::translate(double x, double y)
{
    this->x += x;
    this->y += y;
}

void
Point::translate(const Vector &vector)
{
    this->translate(vector.x, vector.y);
}

void
Point::rotate(double angle)
{
    double cur_x = (double)this->x;
    double cur_y = (double)this->y;
    double s     = sin(angle);
    double c     = cos(angle);
    this->x = (coord_t)round(c * cur_x - s * cur_y);
    this->y = (coord_t)round(c * cur_y + s * cur_x);
}

void
Point::rotate(double angle, const Point &center)
{
    double cur_x = (double)this->x;
    double cur_y = (double)this->y;
    double s     = sin(angle);
    double c     = cos(angle);
    double dx    = cur_x - (double)center.x;
    double dy    = cur_y - (double)center.y;
    this->x = (coord_t)round( (double)center.x + c * dx - s * dy );
    this->y = (coord_t)round( (double)center.y + c * dy + s * dx );
}

bool
Point::coincides_with_epsilon(const Point &point) const
{
    return std::abs(this->x - point.x) < SCALED_EPSILON && std::abs(this->y - point.y) < SCALED_EPSILON;
}

int
Point::nearest_point_index(const Points &points) const
{
    PointConstPtrs p;
    p.reserve(points.size());
    for (Points::const_iterator it = points.begin(); it != points.end(); ++it)
        p.push_back(&*it);
    return this->nearest_point_index(p);
}

int Point::nearest_point_index(const PointConstPtrs &points) const
{
    int idx = -1;
    double distance = -1;  // double because long is limited to 2147483647 on some platforms and it's not enough
    
    for (PointConstPtrs::const_iterator it = points.begin(); it != points.end(); ++it) {
        /* If the X distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        double d = sqr<double>(this->x - (*it)->x);
        if (distance != -1 && d > distance) continue;
        
        /* If the Y distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        d += sqr<double>(this->y - (*it)->y);
        if (distance != -1 && d > distance) continue;
        
        idx = it - points.begin();
        distance = d;
        
        if (distance < EPSILON) break;
    }
    
    return idx;
}

int
Point::nearest_point_index(const PointPtrs &points) const
{
    PointConstPtrs p;
    p.reserve(points.size());
    for (PointPtrs::const_iterator it = points.begin(); it != points.end(); ++it)
        p.push_back(*it);
    return this->nearest_point_index(p);
}

bool
Point::nearest_point(const Points &points, Point* point) const
{
    int idx = this->nearest_point_index(points);
    if (idx == -1) return false;
    *point = points.at(idx);
    return true;
}

/* distance to the closest point of line */
double
Point::distance_to(const Line &line) const
{
    const double dx = line.b.x - line.a.x;
    const double dy = line.b.y - line.a.y;
    
    const double l2 = dx*dx + dy*dy;  // avoid a sqrt
    if (l2 == 0.0) return this->distance_to(line.a);   // line.a == line.b case
    
    // Consider the line extending the segment, parameterized as line.a + t (line.b - line.a).
    // We find projection of this point onto the line. 
    // It falls where t = [(this-line.a) . (line.b-line.a)] / |line.b-line.a|^2
    const double t = ((this->x - line.a.x) * dx + (this->y - line.a.y) * dy) / l2;
    if (t < 0.0)      return this->distance_to(line.a);  // beyond the 'a' end of the segment
    else if (t > 1.0) return this->distance_to(line.b);  // beyond the 'b' end of the segment
    Point projection(
        line.a.x + t * dx,
        line.a.y + t * dy
    );
    return this->distance_to(projection);
}

double
Point::perp_distance_to(const Line &line) const
{
    if (line.a.coincides_with(line.b)) return this->distance_to(line.a);
    
    double n = (double)(line.b.x - line.a.x) * (double)(line.a.y - this->y)
        - (double)(line.a.x - this->x) * (double)(line.b.y - line.a.y);
    
    return std::abs(n) / line.length();
}

/* Three points are a counter-clockwise turn if ccw > 0, clockwise if
 * ccw < 0, and collinear if ccw = 0 because ccw is a determinant that
 * gives the signed area of the triangle formed by p1, p2 and this point.
 * In other words it is the 2D cross product of p1-p2 and p1-this, i.e.
 * z-component of their 3D cross product.
 * We return double because it must be big enough to hold 2*max(|coordinate|)^2
 */
double
Point::ccw(const Point &p1, const Point &p2) const
{
    return (double)(p2.x - p1.x)*(double)(this->y - p1.y) - (double)(p2.y - p1.y)*(double)(this->x - p1.x);
}

double
Point::ccw(const Line &line) const
{
    return this->ccw(line.a, line.b);
}

// returns the CCW angle between this-p1 and this-p2
// i.e. this assumes a CCW rotation from p1 to p2 around this
double
Point::ccw_angle(const Point &p1, const Point &p2) const
{
    double angle = atan2(p1.x - this->x, p1.y - this->y)
                 - atan2(p2.x - this->x, p2.y - this->y);
    
    // we only want to return only positive angles
    return angle <= 0 ? angle + 2*PI : angle;
}

Point
Point::projection_onto(const MultiPoint &poly) const
{
    Point running_projection = poly.first_point();
    double running_min = this->distance_to(running_projection);
    
    Lines lines = poly.lines();
    for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
        Point point_temp = this->projection_onto(*line);
        if (this->distance_to(point_temp) < running_min) {
	        running_projection = point_temp;
	        running_min = this->distance_to(running_projection);
        }
    }
    return running_projection;
}

Point
Point::projection_onto(const Line &line) const
{
    if (line.a.coincides_with(line.b)) return line.a;
    
    /*
        (Ported from VisiLibity by Karl J. Obermeyer)
        The projection of point_temp onto the line determined by
        line_segment_temp can be represented as an affine combination
        expressed in the form projection of
        Point = theta*line_segment_temp.first + (1.0-theta)*line_segment_temp.second.
        If theta is outside the interval [0,1], then one of the Line_Segment's endpoints
        must be closest to calling Point.
    */
    double lx = (double)(line.b.x - line.a.x);
    double ly = (double)(line.b.y - line.a.y);
    double theta = ( (double)(line.b.x - this->x)*lx + (double)(line.b.y- this->y)*ly ) 
          / ( sqr<double>(lx) + sqr<double>(ly) );
    
    if (0.0 <= theta && theta <= 1.0)
        return theta * line.a + (1.0-theta) * line.b;
    
    // Else pick closest endpoint.
    if (this->distance_to(line.a) < this->distance_to(line.b)) {
        return line.a;
    } else {
        return line.b;
    }
}

Point
Point::negative() const
{
    return Point(-this->x, -this->y);
}

Vector
Point::vector_to(const Point &point) const
{
    return Vector(point.x - this->x, point.y - this->y);
}

std::ostream&
operator<<(std::ostream &stm, const Pointf &pointf)
{
    return stm << pointf.x << "," << pointf.y;
}

double
Pointf::ccw(const Pointf &p1, const Pointf &p2) const
{
    return (double)(p2.x - p1.x)*(double)(this->y - p1.y) - (double)(p2.y - p1.y)*(double)(this->x - p1.x);
}

std::string
Pointf::wkt() const
{
    std::ostringstream ss;
    ss << "POINT(" << this->x << " " << this->y << ")";
    return ss.str();
}

std::string
Pointf::dump_perl() const
{
    std::ostringstream ss;
    ss << "[" << this->x << "," << this->y << "]";
    return ss.str();
}

void
Pointf::scale(double factor)
{
    this->x *= factor;
    this->y *= factor;
}

void
Pointf::translate(double x, double y)
{
    this->x += x;
    this->y += y;
}

void
Pointf::translate(const Vectorf &vector)
{
    this->translate(vector.x, vector.y);
}

void
Pointf::rotate(double angle)
{
    double cur_x = this->x;
    double cur_y = this->y;
    double s     = sin(angle);
    double c     = cos(angle);
    this->x = c * cur_x - s * cur_y;
    this->y = c * cur_y + s * cur_x;
}

void
Pointf::rotate(double angle, const Pointf &center)
{
    double cur_x = this->x;
    double cur_y = this->y;
    double s     = sin(angle);
    double c     = cos(angle);
    double dx    = cur_x - center.x;
    double dy    = cur_y - center.y;
    this->x = center.x + c * dx - s * dy;
    this->y = center.y + c * dy + s * dx;
}

Pointf
Pointf::negative() const
{
    return Pointf(-this->x, -this->y);
}

Vectorf
Pointf::vector_to(const Pointf &point) const
{
    return Vectorf(point.x - this->x, point.y - this->y);
}

void
Pointf3::scale(double factor)
{
    Pointf::scale(factor);
    this->z *= factor;
}

void
Pointf3::translate(const Vectorf3 &vector)
{
    this->translate(vector.x, vector.y, vector.z);
}

void
Pointf3::translate(double x, double y, double z)
{
    Pointf::translate(x, y);
    this->z += z;
}

double
Pointf3::distance_to(const Pointf3 &point) const
{
    double dx = ((double)point.x - this->x);
    double dy = ((double)point.y - this->y);
    double dz = ((double)point.z - this->z);
    return sqrt(dx*dx + dy*dy + dz*dz);
}

Pointf3
Pointf3::negative() const
{
    return Pointf3(-this->x, -this->y, -this->z);
}

Vectorf3
Pointf3::vector_to(const Pointf3 &point) const
{
    return Vectorf3(point.x - this->x, point.y - this->y, point.z - this->z);
}

namespace int128 {

int orient(const Point &p1, const Point &p2, const Point &p3)
{
    Slic3r::Vector v1(p2 - p1);
    Slic3r::Vector v2(p3 - p1);
    return Int128::sign_determinant_2x2_filtered(v1.x, v1.y, v2.x, v2.y);
}

int cross(const Point &v1, const Point &v2)
{
    return Int128::sign_determinant_2x2_filtered(v1.x, v1.y, v2.x, v2.y);
}

}

}
