#ifndef SLAAUTOSUPPORTS_HPP_
#define SLAAUTOSUPPORTS_HPP_

#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/SLACommon.hpp>

#include <boost/container/small_vector.hpp>

// #define SLA_AUTOSUPPORTS_DEBUG

namespace Slic3r {

class SLAAutoSupports {
public:
    struct Config {
            float density_relative;
            float minimal_distance;
            float head_diameter;
            ///////////////
            inline float support_force() const { return 7.7f / density_relative; } // a force one point can support       (arbitrary force unit)
            inline float tear_pressure() const { return 1.f; }  // pressure that the display exerts    (the force unit per mm2)
        };

    SLAAutoSupports(const TriangleMesh& mesh, const sla::EigenMesh3D& emesh, const std::vector<ExPolygons>& slices,
                     const std::vector<float>& heights, const Config& config, std::function<void(void)> throw_on_cancel, std::function<void(int)> statusfn);
    const std::vector<sla::SupportPoint>& output() { return m_output; }

	struct MyLayer;

    struct Structure {
        Structure(MyLayer &layer, const ExPolygon& poly, const BoundingBox &bbox, const Vec2f &centroid, float area, float h) : 
            layer(&layer), polygon(&poly), bbox(bbox), centroid(centroid), area(area), height(h)
#ifdef SLA_AUTOSUPPORTS_DEBUG
            , unique_id(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()))
#endif /* SLA_AUTOSUPPORTS_DEBUG */
            {}
        MyLayer *layer;
        const ExPolygon* polygon = nullptr;
        const BoundingBox bbox;
        const Vec2f centroid = Vec2f::Zero();
        const float area = 0.f;
        float height = 0;
        // How well is this ExPolygon held to the print base?
        // Positive number, the higher the better.
        float supports_force_this_layer     = 0.f;
        float supports_force_inherited      = 0.f;
        float supports_force_total() const { return this->supports_force_this_layer + this->supports_force_inherited; }
#ifdef SLA_AUTOSUPPORTS_DEBUG
        std::chrono::milliseconds unique_id;
#endif /* SLA_AUTOSUPPORTS_DEBUG */

        struct Link {
			Link(Structure *island, float overlap_area) : island(island), overlap_area(overlap_area) {}
            Structure   *island;
            float        overlap_area;
        };

#ifdef NDEBUG
		// In release mode, use the optimized container.
        boost::container::small_vector<Link, 4> islands_above;
        boost::container::small_vector<Link, 4> islands_below;
#else
		// In debug mode, use the standard vector, which is well handled by debugger visualizer.
		std::vector<Link>					 	islands_above;
		std::vector<Link>						islands_below;
#endif
        // Overhangs, that are dangling considerably.
        ExPolygons                              dangling_areas;
        // Complete overhands.
        ExPolygons                              overhangs;
        // Overhangs, where the surface must slope.
        ExPolygons                              overhangs_slopes;
        float                                   overhangs_area;

        bool overlaps(const Structure &rhs) const { 
            return this->bbox.overlap(rhs.bbox) && (this->polygon->overlaps(*rhs.polygon) || rhs.polygon->overlaps(*this->polygon)); 
        }
        float overlap_area(const Structure &rhs) const { 
            double out = 0.;
            if (this->bbox.overlap(rhs.bbox)) {
                 Polygons polys = intersection(to_polygons(*this->polygon), to_polygons(*rhs.polygon), false);
                 for (const Polygon &poly : polys)
                    out += poly.area();
            }
            return float(out);
        }
        float area_below() const { 
            float area = 0.f; 
            for (const Link &below : this->islands_below) 
                area += below.island->area; 
            return area;
        }
        Polygons polygons_below() const { 
            size_t cnt = 0;
			for (const Link &below : this->islands_below)
                cnt += 1 + below.island->polygon->holes.size();
            Polygons out;
            out.reserve(cnt);
			for (const Link &below : this->islands_below) {
                out.emplace_back(below.island->polygon->contour);
				append(out, below.island->polygon->holes);
            }
            return out;
        }
        ExPolygons expolygons_below() const { 
            ExPolygons out;
            out.reserve(this->islands_below.size());
            for (const Link &below : this->islands_below)
                out.emplace_back(*below.island->polygon);
            return out;
        }
        // Positive deficit of the supports. If negative, this area is well supported. If positive, more supports need to be added.
        float support_force_deficit(const float tear_pressure) const { return this->area * tear_pressure - this->supports_force_total(); }
    };

    struct MyLayer {
		MyLayer(const size_t layer_id, coordf_t print_z) : layer_id(layer_id), print_z(print_z) {}
        size_t                  layer_id;
        coordf_t                print_z;
        std::vector<Structure>  islands;
    };

    struct RichSupportPoint {
        Vec3f        position;
        Structure   *island;
    };

    struct PointGrid3D {
        struct GridHash {
            std::size_t operator()(const Vec3i &cell_id) const {
                return std::hash<int>()(cell_id.x()) ^ std::hash<int>()(cell_id.y() * 593) ^ std::hash<int>()(cell_id.z() * 7919);
            }
        };
        typedef std::unordered_multimap<Vec3i, RichSupportPoint, GridHash> Grid;

        Vec3f   cell_size;
        Grid    grid;

        Vec3i cell_id(const Vec3f &pos) {
            return Vec3i(int(floor(pos.x() / cell_size.x())),
                         int(floor(pos.y() / cell_size.y())),
                         int(floor(pos.z() / cell_size.z())));
        }

        void insert(const Vec2f &pos, Structure *island) {
            RichSupportPoint pt;
			pt.position = Vec3f(pos.x(), pos.y(), float(island->layer->print_z));
            pt.island   = island;
            grid.emplace(cell_id(pt.position), pt);
        }

        bool collides_with(const Vec2f &pos, Structure *island, float radius) {
            Vec3f pos3d(pos.x(), pos.y(), float(island->layer->print_z));
            Vec3i cell = cell_id(pos3d);
            std::pair<Grid::const_iterator, Grid::const_iterator> it_pair = grid.equal_range(cell);
            if (collides_with(pos3d, radius, it_pair.first, it_pair.second))
                return true;
            for (int i = -1; i < 2; ++ i)
                for (int j = -1; j < 2; ++ j)
                    for (int k = -1; k < 1; ++ k) {
                        if (i == 0 && j == 0 && k == 0)
                            continue;
                        it_pair = grid.equal_range(cell + Vec3i(i, j, k));
                        if (collides_with(pos3d, radius, it_pair.first, it_pair.second))
                            return true;
                    }
            return false;
        }

    private:
        bool collides_with(const Vec3f &pos, float radius, Grid::const_iterator it_begin, Grid::const_iterator it_end) {
            for (Grid::const_iterator it = it_begin; it != it_end; ++ it) {
				float dist2 = (it->second.position - pos).squaredNorm();
                if (dist2 < radius * radius)
                    return true;
            }
            return false;
        }
    };

private:
    std::vector<sla::SupportPoint> m_output;

    SLAAutoSupports::Config m_config;

    void process(const std::vector<ExPolygons>& slices, const std::vector<float>& heights);
    void uniformly_cover(const ExPolygons& islands, Structure& structure, PointGrid3D &grid3d, bool is_new_island = false, bool just_one = false);
    void project_onto_mesh(std::vector<sla::SupportPoint>& points) const;

#ifdef SLA_AUTOSUPPORTS_DEBUG
    static void output_expolygons(const ExPolygons& expolys, const std::string &filename);
    static void output_structures(const std::vector<Structure> &structures);
#endif // SLA_AUTOSUPPORTS_DEBUG

    const sla::EigenMesh3D& m_emesh;
    std::function<void(void)> m_throw_on_cancel;
    std::function<void(int)>  m_statusfn;
};


} // namespace Slic3r


#endif // SLAAUTOSUPPORTS_HPP_
