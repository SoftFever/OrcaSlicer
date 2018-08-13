#ifndef CONCAVEHULL_HPP
#define CONCAVEHULL_HPP

#include <cstdint>
#include <vector>

namespace Slic3r {

class Pointf;

namespace concavehull {

using std::uint64_t;

struct Point
{
    double x = 0.0;
    double y = 0.0;
    std::uint64_t id = 0;

    Point() = default;

    Point(double x, double y)
        : x(x)
        , y(y)
    {}

    explicit Point(const Slic3r::Pointf&);
};

struct PointValue
{
    Point point;
    double distance = 0.0;
    double angle = 0.0;
};

extern const size_t stride; // size in bytes of x, y, id

using PointVector = std::vector<Point>;
using PointValueVector = std::vector<PointValue>;
using LineSegment = std::pair<Point, Point>;

auto ConcaveHull(PointVector &dataset, size_t k, bool iterate) -> PointVector;

}
}

#endif // CONCAVEHULL_HPP
