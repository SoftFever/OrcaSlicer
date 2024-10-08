#include <random>

#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "FuzzySkin.hpp"

#include "libnoise/noise.h"

using namespace Slic3r;

namespace Slic3r::Feature::FuzzySkin {

// Produces a random value between 0 and 1. Thread-safe.
static double random_value() {
    thread_local std::random_device rd;
    // Hash thread ID for random number seed if no hardware rng seed is available
    thread_local std::mt19937 gen(rd.entropy() > 0 ? rd() : std::hash<std::thread::id>()(std::this_thread::get_id()));
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gen);
}

class UniformNoise: public noise::module::Module {
    public:
        UniformNoise(): Module (GetSourceModuleCount ()) {};

        virtual int GetSourceModuleCount() const { return 0; }
        virtual double GetValue(double x, double y, double z) const { return random_value() * 2 - 1; }
};

static std::unique_ptr<noise::module::Module> get_noise_module(const FuzzySkinConfig& cfg) {
    if (cfg.noise_type == NoiseType::Perlin) {
        auto perlin_noise = noise::module::Perlin();
        perlin_noise.SetFrequency(1 / cfg.noise_scale);
        perlin_noise.SetOctaveCount(cfg.noise_octaves);
        perlin_noise.SetPersistence(cfg.noise_persistence);
        return std::make_unique<noise::module::Perlin>(perlin_noise);
    } else if (cfg.noise_type == NoiseType::Billow) {
        auto billow_noise = noise::module::Billow();
        billow_noise.SetFrequency(1 / cfg.noise_scale);
        billow_noise.SetOctaveCount(cfg.noise_octaves);
        billow_noise.SetPersistence(cfg.noise_persistence);
        return std::make_unique<noise::module::Billow>(billow_noise);
    } else if (cfg.noise_type == NoiseType::RidgedMulti) {
        auto ridged_multi_noise = noise::module::RidgedMulti();
        ridged_multi_noise.SetFrequency(1 / cfg.noise_scale);
        ridged_multi_noise.SetOctaveCount(cfg.noise_octaves);
        return std::make_unique<noise::module::RidgedMulti>(ridged_multi_noise);
    } else if (cfg.noise_type == NoiseType::Voronoi) {
        auto voronoi_noise = noise::module::Voronoi();
        voronoi_noise.SetFrequency(1 / cfg.noise_scale);
        voronoi_noise.SetDisplacement(1.0);
        return std::make_unique<noise::module::Voronoi>(voronoi_noise);
    } else {
        return std::make_unique<UniformNoise>();
    }
}

// Thanks Cura developers for this function.
void fuzzy_polyline(Points& poly, bool closed, coordf_t slice_z, const FuzzySkinConfig& cfg)
{
    std::unique_ptr<noise::module::Module> noise = get_noise_module(cfg);

    const double min_dist_between_points = cfg.point_distance * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = cfg.point_distance / 2.;
    double dist_left_over = random_value() * (min_dist_between_points / 2.); // the distance to be traversed on the line before making the first new point
    Point* p0 = &poly.back();
    Points out;
    out.reserve(poly.size());
    for (Point &p1 : poly)
    {
        if (!closed) {
            // Skip the first point for open path
            closed = true;
            p0 = &p1;
            continue;
        }
        // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size;
            p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist)
        {
            Point pa = *p0 + (p0p1 * (p0pa_dist / p0p1_size)).cast<coord_t>();
            double r = noise->GetValue(unscale_(pa.x()), unscale_(pa.y()), slice_z) * cfg.thickness;
            out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * r).cast<coord_t>());
        }
        dist_left_over = p0pa_dist - p0p1_size;
        p0 = &p1;
    }
    while (out.size() < 3) {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0)
            break;
        -- point_idx;
    }
    if (out.size() >= 3)
        poly = std::move(out);
}

// Thanks Cura developers for this function.
void fuzzy_extrusion_line(Arachne::ExtrusionJunctions& ext_lines, coordf_t slice_z, const FuzzySkinConfig& cfg)
{
    std::unique_ptr<noise::module::Module> noise = get_noise_module(cfg);

    const double min_dist_between_points = cfg.point_distance * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = cfg.point_distance / 2.;
    double dist_left_over = random_value() * (min_dist_between_points / 2.); // the distance to be traversed on the line before making the first new point

    auto* p0 = &ext_lines.front();
    Arachne::ExtrusionJunctions out;
    out.reserve(ext_lines.size());
    for (auto& p1 : ext_lines) {
        if (p0->p == p1.p) { // Connect endpoints.
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1 = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist) {
            Point pa = p0->p + (p0p1 * (p0pa_dist / p0p1_size)).cast<coord_t>();
            double r = noise->GetValue(unscale_(pa.x()), unscale_(pa.y()), slice_z) * cfg.thickness;
            out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * r).cast<coord_t>(), p1.w, p1.perimeter_index);
        }
        dist_left_over = p0pa_dist - p0p1_size;
        p0 = &p1;
    }

    while (out.size() < 3) {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
            break;
        --point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p) // Connect endpoints.
        out.front().p = out.back().p;

    if (out.size() >= 3)
        ext_lines = std::move(out);
}

bool should_fuzzify(const FuzzySkinConfig& config, const int layer_id, const size_t loop_idx, const bool is_contour)
{
    const auto fuzziy_type = config.type;

    if (fuzziy_type == FuzzySkinType::None) {
        return false;
    }
    if (!config.fuzzy_first_layer && layer_id <= 0) {
        // Do not fuzzy first layer unless told to
        return false;
    }

    const bool fuzzify_contours = loop_idx == 0 || fuzziy_type == FuzzySkinType::AllWalls;
    const bool fuzzify_holes    = fuzzify_contours && (fuzziy_type == FuzzySkinType::All || fuzziy_type == FuzzySkinType::AllWalls);

    return is_contour ? fuzzify_contours : fuzzify_holes;
}

} // namespace Slic3r::Feature::FuzzySkin
