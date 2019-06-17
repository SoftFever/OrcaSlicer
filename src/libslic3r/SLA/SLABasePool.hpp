#ifndef SLABASEPOOL_HPP
#define SLABASEPOOL_HPP

#include <vector>
#include <functional>
#include <cmath>

namespace Slic3r {

class ExPolygon;
class Polygon;
using ExPolygons = std::vector<ExPolygon>;
using Polygons = std::vector<Polygon>;

class TriangleMesh;

namespace sla {

using ThrowOnCancel = std::function<void(void)>;

/// Calculate the polygon representing the silhouette from the specified height
void base_plate(const TriangleMesh& mesh,       // input mesh
                ExPolygons& output,             // Output will be merged with
                float samplingheight = 0.1f,    // The height range to sample
                float layerheight = 0.05f,      // The sampling height
                ThrowOnCancel thrfn = [](){});  // Will be called frequently

void base_plate(const TriangleMesh& mesh,       // input mesh
                ExPolygons& output,             // Output will be merged with
                const std::vector<float>&,      // Exact Z levels to sample
                ThrowOnCancel thrfn = [](){});  // Will be called frequently

// Function to cut tiny connector cavities for a given polygon. The input poly
// will be offsetted by "padding" and small rectangle shaped cavities will be
// inserted along the perimeter in every "stride" distance. The stick rectangles
// will have a with about "stick_width". The input dimensions are in world 
// measure, not the scaled clipper units.
void breakstick_holes(ExPolygon &poly,
                      double     padding,
                      double     stride,
                      double     stick_width,
                      double     penetration = 0.0);

struct PoolConfig {
    double min_wall_thickness_mm = 2;
    double min_wall_height_mm = 5;
    double max_merge_distance_mm = 50;
    double edge_radius_mm = 1;
    double wall_slope = std::atan(1.0);          // Universal constant for Pi/4
    struct EmbedObject {
        double object_gap_mm = 0.5;
        double stick_stride_mm = 10;
        double stick_width_mm = 0.3;
        double stick_penetration_mm = 0.1;
        bool enabled = false;
        operator bool() const { return enabled; }
    } embed_object;

    ThrowOnCancel throw_on_cancel = [](){};

    inline PoolConfig() {}
    inline PoolConfig(double wt, double wh, double md, double er, double slope):
        min_wall_thickness_mm(wt),
        min_wall_height_mm(wh),
        max_merge_distance_mm(md),
        edge_radius_mm(er),
        wall_slope(slope) {}
};

/// Calculate the pool for the mesh for SLA printing
void create_base_pool(const Polygons& base_plate,
                      TriangleMesh& output_mesh,
                      const ExPolygons& holes,
                      const PoolConfig& = PoolConfig());

/// Returns the elevation needed for compensating the pad.
inline double get_pad_elevation(const PoolConfig& cfg) {
    return cfg.min_wall_thickness_mm;
}

inline double get_pad_fullheight(const PoolConfig& cfg) {
    return cfg.min_wall_height_mm + cfg.min_wall_thickness_mm;
}

}

}

#endif // SLABASEPOOL_HPP
