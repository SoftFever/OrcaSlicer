#include "Clipper2Utils.hpp"

namespace Slic3r {

//BBS: FIXME
Slic3r::Polylines Paths64_to_polylines(const Clipper2Lib::Paths64& in)
{
    Slic3r::Polylines out;
    out.reserve(in.size());
    for (const Clipper2Lib::Path64& path64 : in) {
        Slic3r::Points points;
        points.reserve(path64.size());
        for (const Clipper2Lib::Point64& point64 : path64)
            points.emplace_back(std::move(Slic3r::Point(point64.x, point64.y)));
        out.emplace_back(std::move(Slic3r::Polyline(points)));
    }
    return out;
}

//BBS: FIXME
template <typename T>
Clipper2Lib::Paths64 Slic3rPoints_to_Paths64(const std::vector<T>& in)
{
    Clipper2Lib::Paths64 out;
    out.reserve(in.size());
    for (const T& item: in) {
        Clipper2Lib::Path64 path;
        path.reserve(item.size());
        for (const Slic3r::Point& point : item.points)
            path.emplace_back(std::move(Clipper2Lib::Point64(point.x(), point.y())));
        out.emplace_back(std::move(path));
    }
    return out;
}

Polylines _clipper2_pl_open(Clipper2Lib::ClipType clipType, const Slic3r::Polylines& subject, const Slic3r::Polygons& clip)
{
    Clipper2Lib::Clipper64 c;
    c.AddOpenSubject(Slic3rPoints_to_Paths64(subject));
    c.AddClip(Slic3rPoints_to_Paths64(clip));

    Clipper2Lib::ClipType ct = clipType;
    Clipper2Lib::FillRule fr = Clipper2Lib::FillRule::NonZero;
    Clipper2Lib::Paths64 solution, solution_open;
    c.Execute(ct, fr, solution, solution_open);

    Slic3r::Polylines out;
    out.reserve(solution.size() + solution_open.size());
    polylines_append(out, std::move(Paths64_to_polylines(solution)));
    polylines_append(out, std::move(Paths64_to_polylines(solution_open)));

    return out;
}

Slic3r::Polylines intersection_pl_2(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip)
    { return _clipper2_pl_open(Clipper2Lib::ClipType::Intersection, subject, clip); }
Slic3r::Polylines  diff_pl_2(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip)
    { return _clipper2_pl_open(Clipper2Lib::ClipType::Difference, subject, clip); }

}