#include "TriangulateWall.hpp"
#include "MTUtils.hpp"

namespace Slic3r {

class Ring {
    size_t idx = 0, nextidx = 1, startidx = 0, begin = 0, end = 0;
    
public:
    explicit Ring(size_t from, size_t to) : begin(from), end(to) { init(begin); }

    size_t size() const { return end - begin; }
    std::pair<size_t, size_t> pos() const { return {idx, nextidx}; }
    bool is_lower() const { return idx < size(); }
    
    void inc()
    {
        if (nextidx != startidx) nextidx++;
        if (nextidx == end) nextidx = begin;
        idx ++;
        if (idx == end) idx = begin;
    }
    
    void init(size_t pos)
    {
        startidx = begin + (pos - begin) % size();
        idx = startidx;
        nextidx = begin + (idx + 1 - begin) % size();
    }
    
    bool is_finished() const { return nextidx == idx; }
};

static double sq_dst(const Vec3d &v1, const Vec3d& v2)
{
    Vec3d v = v1 - v2;
    return v.x() * v.x() + v.y() * v.y() /*+ v.z() * v.z()*/;
}

static double score(const Ring& onring, const Ring &offring,
                    const std::vector<Vec3d> &pts)
{
    double a = sq_dst(pts[onring.pos().first], pts[offring.pos().first]);
    double b = sq_dst(pts[onring.pos().second], pts[offring.pos().first]);
    return (std::abs(a) + std::abs(b)) / 2.;
}

class Triangulator {
    const std::vector<Vec3d> *pts;
    Ring *onring, *offring;
    
    double calc_score() const
    {
        return Slic3r::score(*onring, *offring, *pts);
    }
    
    void synchronize_rings()
    {
        Ring lring = *offring;
        auto minsc = Slic3r::score(*onring, lring, *pts);
        size_t imin = lring.pos().first;
        
        lring.inc();
        
        while(!lring.is_finished()) {
            double score = Slic3r::score(*onring, lring, *pts);
            if (score < minsc) { minsc = score; imin = lring.pos().first; }
            lring.inc();
        }
        
        offring->init(imin);
    }
    
    void emplace_indices(std::vector<Vec3i> &indices)
    {
        Vec3i tr{int(onring->pos().first), int(onring->pos().second),
                 int(offring->pos().first)};
        if (onring->is_lower()) std::swap(tr(0), tr(1));
        indices.emplace_back(tr);
    }
    
public:
    void run(std::vector<Vec3i> &indices)
    {   
        synchronize_rings();
        
        double score = 0, prev_score = 0;        
        while (!onring->is_finished() || !offring->is_finished()) {
            prev_score = score;
            if (onring->is_finished() || (score = calc_score()) > prev_score) {
                std::swap(onring, offring);
            } else {
                emplace_indices(indices);
                onring->inc();
            }
        }
    }

    explicit Triangulator(const std::vector<Vec3d> *points,
                          Ring &                    lower,
                          Ring &                    upper)
        : pts{points}, onring{&upper}, offring{&lower}
    {}
};

Wall triangulate_wall(
    const Polygon &          lower,
    const Polygon &          upper,
    double                   lower_z_mm,
    double                   upper_z_mm)
{
    if (upper.points.size() < 3 || lower.points.size() < 3) return {};
    
    Wall wall;
    auto &pts = wall.first;
    auto &ind = wall.second;

    pts.reserve(lower.points.size() + upper.points.size());
    for (auto &p : lower.points)
        wall.first.emplace_back(unscaled(p.x()), unscaled(p.y()), lower_z_mm);
    for (auto &p : upper.points)
        wall.first.emplace_back(unscaled(p.x()), unscaled(p.y()), upper_z_mm);
    
    ind.reserve(2 * (lower.size() + upper.size()));
    
    Ring lring{0, lower.points.size()}, uring{lower.points.size(), pts.size()};
    Triangulator t{&pts, lring, uring};
    t.run(ind);
    
    return wall;
}

} // namespace Slic3r
