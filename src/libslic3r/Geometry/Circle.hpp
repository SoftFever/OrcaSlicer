#ifndef slic3r_Geometry_Circle_hpp_
#define slic3r_Geometry_Circle_hpp_

namespace Slic3r { namespace Geometry {

/// Find the center of the circle corresponding to the vector of Points as an arc.
Point circle_center_taubin_newton(const Points::const_iterator& input_start, const Points::const_iterator& input_end, size_t cycles = 20);
inline Point circle_center_taubin_newton(const Points& input, size_t cycles = 20) { return circle_center_taubin_newton(input.cbegin(), input.cend(), cycles); }

/// Find the center of the circle corresponding to the vector of Pointfs as an arc.
Vec2d circle_center_taubin_newton(const Vec2ds::const_iterator& input_start, const Vec2ds::const_iterator& input_end, size_t cycles = 20);
inline Vec2d circle_center_taubin_newton(const Vec2ds& input, size_t cycles = 20) { return circle_center_taubin_newton(input.cbegin(), input.cend(), cycles); }

// https://en.wikipedia.org/wiki/Circumscribed_circle
// Circumcenter coordinates, Cartesian coordinates
template<typename Vector>
bool circle_center(const Vector &a, Vector b, Vector c, Vector &center)
{
    using Scalar = typename Vector::Scalar;
    b -= a;
    c -= a;
    if (Scalar d = b.x() * c.y() - b.y() * c.x(); d == 0)
        return false;
    else {
        Vector v = c.squaredNorm() * b - b.squaredNorm() * c;
        center = a + Vector(- v.y(), v.x()) / (2 * d);
        return true;
    }
}

/*
// Likely a bit more accurate accurate version of circle_center() by centering.
template<typename Vector>
bool circle_center_centered(Vector a, Vector b, Vector c, Vector &center) 
{
    auto bbox_center = 0.5 * (a.cwiseMin(b).cwiseMin(c) + a.cwiseMax(b).cwiseMax(c));
    a -= bbox_center;
    b -= bbox_center;
    c -= bbox_center;
    auto bc = b - c;
    auto ca = c - a;
    auto ab = a - b;
    if (d = ao.x() * bc.y() + bo.x() * ca.y() + co.x() * ab.y(); d == 0)
        return false;
    else {
        center = bbox_center - perp(ao.squaredNorm() * bc + bo.squaredNorm() * ca + co.squaredNorm() * ab) / (2 * d);
        return true;
    }
}
*/

// 2D circle defined by its center and squared radius
template<typename Vector>
struct CircleSq {
    using Scalar = typename Vector::Scalar;

    Vector center;
    Scalar radius2;

    CircleSq(const Vector &a, const Vector &b) : center(Scalar(0.5) * (a + b)) { radius2 = (a - center).squaredNorm(); }
    CircleSq(const Vector &a, const Vector &b, const Vector &c) {
        if (circle_center(a, b, c, this->center))
            this->radius = (a - this->center).squaredNorm();
        else
            *this = make_invalid();
    }

    bool invalid() const { return this->radius2 < 0; }
    bool valid() const { return ! this->invalid(); }
    bool contains(const Vector &p) const { return (p - this->center).squaredNorm() < this->radius2; }
    bool contains_with_eps(const Vector &p, const Scalar relative_epsilon2 = Scalar((1 + 1e-14) * (1 + 1e-14))) const 
        { Scalar r2 = this->radius2 * relative_epsilon2; return (p - this->center).squaredNorm() < r2; }

    static CircleSq make_invalid() { return CircleSq { { 0, 0 }, -1 }; }
};

// 2D circle defined by its center and radius
template<typename Vector>
struct Circle {
    using Scalar = typename Vector::Scalar;

    Vector center;
    Scalar radius;

    Circle(const Vector &a, const Vector &b) : center(Scalar(0.5) * (a + b)) { radius = (a - center).norm(); }
    Circle(const Vector &a, const Vector &b, const Vector &c) {
        this->center = circle_center(a, b, c);
        this->radius = (a - this->center).norm();
    }
    template<typename Vector2>
    explicit Circle(const CircleSq<Vector2> &c) : center(c.center), radius(sqrt(c.radius2)) {}

    bool invalid() const { return this->radius < 0; }
    bool valid() const { return ! this->invalid(); }
    bool contains(const Vector &p) const { return (p - this->center).squaredNorm() < this->radius * this->radius; }
    bool contains_with_eps(const Vector &p, const Scalar relative_epsilon = 1 + 1e-14) const { Scalar r = this->radius * relative_epsilon; return (p - this->center).squaredNorm() < r * r; }

    static Circle make_invalid() { return Circle { { 0, 0 }, -1 }; }
};

// Randomized algorithm by Emo Welzl
template<typename Vector, typename Points>
CircleSq<Vector> smallest_enclosing_circle2_welzl(const Points &points)
{
    using Scalar = typename Vector::Scalar;
    
    const auto &p0     = points[0].cast<Scalar>();
    auto        circle = CircleSq<Vector>(p0, points[1].cast<Scalar>());
    for (size_t i = 2; i < points.size(); ++ i)
        if (const Vector &p = points[i].cast<Scalar>(); ! circle.contains_with_eps(p)) {
            // p is the first point on the smallest circle enclosing points[0..i]
            auto c = CircleSq<Vector>(p0, p);
            for (size_t j = 1; j < i; ++ j)
                if (const Vector &q = points[j].cast<Scalar>(); ! c.contains_with_eps(q)) {
                    // q is the second point on the smallest circle enclosing points[0..i]
                    auto c2 = CircleSq<Vector>(p, q);
                    for (int k = 0; k < j; ++ k)
                        if (const Vector &r = points[k].cast<Scalar>(); ! c2.contains_with_eps(r)) {
                            if (Vector center; circle_center<Vector>(p, q, r, center)) {
                                c2.center  = center;
                                c2.radius2 = (center - p).squaredNorm();
                            }
                        }
                    if (c2.radius2 > 0)
                        c = c2;
                }
            if (c.radius2 > 0)
                circle = c;
        }
    return circle;
}

template<typename Vector, typename Points>
Circle<Vector> smallest_enclosing_circle_welzl(const Points &points)
{
    return Circle<Vector>(smallest_enclosing_circle2_welzl<Vector, Points>(points));
}

// Ugly named variant, that accepts the squared line 
// Don't call me with a nearly zero length vector!
// sympy: 
// factor(solve([a * x + b * y + c, x**2 + y**2 - r**2], [x, y])[0])
// factor(solve([a * x + b * y + c, x**2 + y**2 - r**2], [x, y])[1])
template<typename T>
int ray_circle_intersections_r2_lv2_c(T r2, T a, T b, T lv2, T c, std::pair<Eigen::Matrix<T, 2, 1, Eigen::DontAlign>, Eigen::Matrix<T, 2, 1, Eigen::DontAlign>> &out)
{
    T x0 = - a * c;
    T y0 = - b * c;
    T d2 = r2 * lv2 - c * c;
    if (d2 < T(0))
        return 0;
    T d = sqrt(d2);
    out.first.x() = (x0 + b * d) / lv2;
    out.first.y() = (y0 - a * d) / lv2;
    out.second.x() = (x0 - b * d) / lv2;
    out.second.y() = (y0 + a * d) / lv2;
    return d == T(0) ? 1 : 2;
}
template<typename T>
int ray_circle_intersections(T r, T a, T b, T c, std::pair<Eigen::Matrix<T, 2, 1, Eigen::DontAlign>, Eigen::Matrix<T, 2, 1, Eigen::DontAlign>> &out)
{
    T lv2 = a * a + b * b;
    if (lv2 < T(SCALED_EPSILON * SCALED_EPSILON)) {
        //FIXME what is the correct epsilon?
        // What if the line touches the circle?
        return false;
    }
    return ray_circle_intersections_r2_lv2_c2(r * r, a, b, a * a + b * b, c, out);
}

} } // namespace Slic3r::Geometry

#endif // slic3r_Geometry_Circle_hpp_
