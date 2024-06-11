//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_LINEAR_ALG_2D_H
#define UTILS_LINEAR_ALG_2D_H

#include "../../Point.hpp"

namespace Slic3r::Arachne::LinearAlg2D
{

/*!
     * Test whether a point is inside a corner.
     * Whether point \p query_point is left of the corner abc.
     * Whether the \p query_point is in the circle half left of ab and left of bc, rather than to the right.
     *
     * Test whether the \p query_point is inside of a polygon w.r.t a single corner.
 */
inline static bool isInsideCorner(const Point &a, const Point &b, const Point &c, const Vec2i64 &query_point)
{
    //     Visualisation for the algorithm below:
    //
    //                 query
    //                   |
    //                   |
    //                   |
    //    perp-----------b
    //                  / \       (note that the lines
    //                 /   \      AB and AC are normalized
    //                /     \     to 10000 units length)
    //               a       c
    //

    auto normal = [](const Point &p0, coord_t len) -> Point {
        int64_t _len = p0.norm();
        if (_len < 1)
            return {len, 0};
        return (p0.cast<int64_t>() * int64_t(len) / _len).cast<coord_t>();
    };

    auto rotate_90_degree_ccw = [](const Vec2d &p) -> Vec2d {
        return {-p.y(), p.x()};
    };

    constexpr coord_t normal_length = 10000; //Create a normal vector of reasonable length in order to reduce rounding error.
    const Point ba = normal(a - b, normal_length);
    const Point bc = normal(c - b, normal_length);
    const Vec2d bq = query_point.cast<double>() - b.cast<double>();
    const Vec2d perpendicular = rotate_90_degree_ccw(bq); //The query projects to this perpendicular to coordinate 0.

    const double project_a_perpendicular = ba.cast<double>().dot(perpendicular); //Project vertex A on the perpendicular line.
    const double project_c_perpendicular = bc.cast<double>().dot(perpendicular); //Project vertex C on the perpendicular line.
    if ((project_a_perpendicular > 0.) != (project_c_perpendicular > 0.)) //Query is between A and C on the projection.
    {
        return project_a_perpendicular > 0.; //Due to the winding order of corner ABC, this means that the query is inside.
    }
    else //Beyond either A or C, but it could still be inside of the polygon.
    {
        const double project_a_parallel = ba.cast<double>().dot(bq); //Project not on the perpendicular, but on the original.
        const double project_c_parallel = bc.cast<double>().dot(bq);

        //Either:
        // * A is to the right of B (project_a_perpendicular > 0) and C is below A (project_c_parallel < project_a_parallel), or
        // * A is to the left of B (project_a_perpendicular < 0) and C is above A (project_c_parallel > project_a_parallel).
        return (project_c_parallel < project_a_parallel) == (project_a_perpendicular > 0.);
    }
}

/*!
     * Returns the determinant of the 2D matrix defined by the the vectors ab and ap as rows.
     * 
     * The returned value is zero for \p p lying (approximately) on the line going through \p a and \p b
     * The value is positive for values lying to the left and negative for values lying to the right when looking from \p a to \p b.
     * 
     * \param p the point to check
     * \param a the from point of the line
     * \param b the to point of the line
     * \return a positive value when \p p lies to the left of the line from \p a to \p b
 */
static inline int64_t pointIsLeftOfLine(const Point &p, const Point &a, const Point &b)
{
    return int64_t(b.x() - a.x()) * int64_t(p.y() - a.y()) - int64_t(b.y() - a.y()) * int64_t(p.x() - a.x());
}

/*!
     * Compute the angle between two consecutive line segments.
     *
     * The angle is computed from the left side of b when looking from a.
     *
     *   c
     *    \                     .
     *     \ b
     * angle|
     *      |
     *      a
     *
     * \param a start of first line segment
     * \param b end of first segment and start of second line segment
     * \param c end of second line segment
     * \return the angle in radians between 0 and 2 * pi of the corner in \p b
 */
static inline float getAngleLeft(const Point &a, const Point &b, const Point &c)
{
    const Vec2i64 ba   = (a - b).cast<int64_t>();
    const Vec2i64 bc   = (c - b).cast<int64_t>();
    const int64_t dott = ba.dot(bc);      // dot product
    const int64_t det  = cross2(ba, bc); // determinant
    if (det == 0) {
        if ((ba.x() != 0 && (ba.x() > 0) == (bc.x() > 0)) || (ba.x() == 0 && (ba.y() > 0) == (bc.y() > 0)))
            return 0; // pointy bit
        else
            return float(M_PI); // straight bit
    }
    const float angle = -atan2(double(det), double(dott)); // from -pi to pi
    if (angle >= 0)
        return angle;
    else
        return M_PI * 2 + angle;
}

}//namespace Slic3r::Arachne
#endif//UTILS_LINEAR_ALG_2D_H
