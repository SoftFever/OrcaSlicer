#include "igl/random_points_on_mesh.h"
#include "igl/AABB.h"

#include <tbb/parallel_for.h>

#include "SLAAutoSupports.hpp"
#include "Model.hpp"
#include "ExPolygon.hpp"
#include "SVG.hpp"
#include "Point.hpp"
#include "ClipperUtils.hpp"
#include "Tesselate.hpp"
#include "libslic3r.h"

#include <iostream>
#include <random>

namespace Slic3r {

/*float SLAAutoSupports::approximate_geodesic_distance(const Vec3d& p1, const Vec3d& p2, Vec3d& n1, Vec3d& n2)
{
    n1.normalize();
    n2.normalize();

    Vec3d v = (p2-p1);
    v.normalize();

    float c1 = n1.dot(v);
    float c2 = n2.dot(v);
    float result = pow(p1(0)-p2(0), 2) + pow(p1(1)-p2(1), 2) + pow(p1(2)-p2(2), 2);
    // Check for division by zero:
    if(fabs(c1 - c2) > 0.0001)
        result *= (asin(c1) - asin(c2)) / (c1 - c2);
    return result;
}


float SLAAutoSupports::get_required_density(float angle) const
{
    // calculation would be density_0 * cos(angle). To provide one more degree of freedom, we will scale the angle
    // to get the user-set density for 45 deg. So it ends up as density_0 * cos(K * angle).
    float K = 4.f * float(acos(m_config.density_at_45/m_config.density_at_horizontal) / M_PI);
    return std::max(0.f, float(m_config.density_at_horizontal * cos(K*angle)));
}

float SLAAutoSupports::distance_limit(float angle) const
{
    return 1./(2.4*get_required_density(angle));
}*/

SLAAutoSupports::SLAAutoSupports(const TriangleMesh& mesh, const sla::EigenMesh3D& emesh, const std::vector<ExPolygons>& slices, const std::vector<float>& heights,
                                   const Config& config, std::function<void(void)> throw_on_cancel)
: m_config(config), m_emesh(emesh), m_throw_on_cancel(throw_on_cancel)
{
    process(slices, heights);
    project_onto_mesh(m_output);
}

void SLAAutoSupports::project_onto_mesh(std::vector<sla::SupportPoint>& points) const
{
    // The function  makes sure that all the points are really exactly placed on the mesh.
    igl::Hit hit_up{0, 0, 0.f, 0.f, 0.f};
    igl::Hit hit_down{0, 0, 0.f, 0.f, 0.f};

    // Use a reasonable granularity to account for the worker thread synchronization cost.
    tbb::parallel_for(tbb::blocked_range<size_t>(0, points.size(), 64),
        [this, &points](const tbb::blocked_range<size_t>& range) {
            for (size_t point_id = range.begin(); point_id < range.end(); ++ point_id) {
                if ((point_id % 16) == 0)
                    // Don't call the following function too often as it flushes CPU write caches due to synchronization primitves.
                    m_throw_on_cancel();
                Vec3f& p = points[point_id].pos;
                // Project the point upward and downward and choose the closer intersection with the mesh.
                //bool up   = igl::ray_mesh_intersect(p.cast<float>(), Vec3f(0., 0., 1.), m_V, m_F, hit_up);
                //bool down = igl::ray_mesh_intersect(p.cast<float>(), Vec3f(0., 0., -1.), m_V, m_F, hit_down);

                sla::EigenMesh3D::hit_result hit_up   = m_emesh.query_ray_hit(p.cast<double>(), Vec3d(0., 0., 1.));
                sla::EigenMesh3D::hit_result hit_down = m_emesh.query_ray_hit(p.cast<double>(), Vec3d(0., 0., -1.));

                bool up   = hit_up.face() != -1;
                bool down = hit_down.face() != -1;

                if (!up && !down)
                    continue;

                sla::EigenMesh3D::hit_result& hit = (!down || (hit_up.distance() < hit_down.distance())) ? hit_up : hit_down;
                //int fid = hit.face();
                //Vec3f bc(1-hit.u-hit.v, hit.u, hit.v);
                //p = (bc(0) * m_V.row(m_F(fid, 0)) + bc(1) * m_V.row(m_F(fid, 1)) + bc(2)*m_V.row(m_F(fid, 2))).cast<float>();

                p = p + (hit.distance() * hit.direction()).cast<float>();
            }
        });
}

void SLAAutoSupports::process(const std::vector<ExPolygons>& slices, const std::vector<float>& heights)
{
    std::vector<std::pair<ExPolygon, coord_t>> islands;
    std::vector<Structure> structures_old;
    std::vector<Structure> structures_new;

    for (unsigned int i = 0; i<slices.size(); ++i) {
        const ExPolygons& expolys_top = slices[i];

        //FIXME WTF?
        const float height = (i>2 ? heights[i-3] : heights[0]-(heights[1]-heights[0]));
        const float layer_height = (i!=0 ? heights[i]-heights[i-1] : heights[0]);
        
        const float safe_angle = 5.f * (float(M_PI)/180.f); // smaller number - less supports
        const float between_layers_offset =  float(scale_(layer_height / std::tan(safe_angle)));

        // FIXME: calculate actual pixel area from printer config:
        //const float pixel_area = pow(wxGetApp().preset_bundle->project_config.option<ConfigOptionFloat>("display_width") / wxGetApp().preset_bundle->project_config.option<ConfigOptionInt>("display_pixels_x"), 2.f); //
        const float pixel_area = pow(0.047f, 2.f);

        // Check all ExPolygons on this slice and check whether they are new or belonging to something below.
        for (const ExPolygon& polygon : expolys_top) {
            float area = float(polygon.area() * SCALING_FACTOR * SCALING_FACTOR);
            if (area < pixel_area)
                continue;
            //FIXME this is not a correct centroid of a polygon with holes.
            structures_new.emplace_back(polygon, get_extents(polygon.contour), Slic3r::unscale(polygon.contour.centroid()).cast<float>(), area, height);
            Structure& top = structures_new.back();
            //FIXME This has a quadratic time complexity, it will be excessively slow for many tiny islands.
            // At least it is now using a bounding box check for pre-filtering.
            for (Structure& bottom : structures_old)
                if (top.overlaps(bottom)) {
                    top.structures_below.push_back(&bottom);
                    float centroids_dist = (bottom.centroid - top.centroid).norm();
                    // Penalization resulting from centroid offset:
//                  bottom.supports_force *= std::min(1.f, 1.f - std::min(1.f, (1600.f * layer_height) * centroids_dist * centroids_dist / bottom.area));
                    bottom.supports_force *= std::min(1.f, 1.f - std::min(1.f, 80.f * centroids_dist * centroids_dist / bottom.area));
                    // Penalization resulting from increasing polygon area:
                    bottom.supports_force *= std::min(1.f, 20.f * bottom.area / top.area);
                }
        }

        // Let's assign proper support force to each of them:
        for (const Structure& below : structures_old) {
            std::vector<Structure*> above_list;
            float above_area = 0.f;
            for (Structure& new_str : structures_new)
                for (const Structure* below1 : new_str.structures_below)
                    if (&below == below1) {
                        above_list.push_back(&new_str);
                        above_area += above_list.back()->area;
                    }
            for (Structure* above : above_list)
                above->supports_force += below.supports_force * above->area / above_area;
        }
        
        // Now iterate over all polygons and append new points if needed.
        for (Structure& s : structures_new) {
            if (s.structures_below.empty()) // completely new island - needs support no doubt
                uniformly_cover(*s.polygon, s, true);
            else
                // Let's see if there's anything that overlaps enough to need supports:
                // What we now have in polygons needs support, regardless of what the forces are, so we can add them.
                for (const ExPolygon& p : diff_ex(to_polygons(*s.polygon), offset(s.expolygons_below(), between_layers_offset)))
                    //FIXME is it an island point or not? Vojtech thinks it is.
                    uniformly_cover(p, s);
        }

        // We should also check if current support is enough given the polygon area.
        for (Structure& s : structures_new) {
            // Areas not supported by the areas below.
            ExPolygons e = diff_ex(to_polygons(*s.polygon), s.polygons_below());
            float e_area = 0.f;
            for (const ExPolygon &ex : e)
                e_area += float(ex.area());
            // Penalization resulting from large diff from the last layer:
//            s.supports_force /= std::max(1.f, (layer_height / 0.3f) * e_area / s.area);
            s.supports_force /= std::max(1.f, 0.17f * (e_area * float(SCALING_FACTOR * SCALING_FACTOR)) / s.area);

            if (s.area * m_config.tear_pressure > s.supports_force) {
                //FIXME Don't calculate area inside the compare function!
                //FIXME Cover until the force deficit is covered. Cover multiple areas, sort by decreasing area.
                ExPolygons::iterator largest_it = std::max_element(e.begin(), e.end(), [](const ExPolygon& a, const ExPolygon& b) { return a.area() < b.area(); });
                if (!e.empty())
                    //FIXME add the support force deficit as a parameter, only cover until the defficiency is covered.
                    uniformly_cover(*largest_it, s);
            }
        }

        // All is done. Prepare to advance to the next layer. 
        structures_old = std::move(structures_new);
        structures_new.clear();

        m_throw_on_cancel();

#ifdef SLA_AUTOSUPPORTS_DEBUG
        /*std::string layer_num_str = std::string((i<10 ? "0" : "")) + std::string((i<100 ? "0" : "")) + std::to_string(i);
        output_expolygons(expolys_top, "top" + layer_num_str + ".svg");
        output_expolygons(diff, "diff" + layer_num_str + ".svg");
        if (!islands.empty())
            output_expolygons(islands, "islands" + layer_num_str + ".svg");*/
#endif /* SLA_AUTOSUPPORTS_DEBUG */
    }
}

std::vector<Vec2f> sample_expolygon(const ExPolygon &expoly, float samples_per_mm2, std::mt19937 &rng)
{    
    // Triangulate the polygon with holes into triplets of 3D points.
    std::vector<Vec2f> triangles = Slic3r::triangulate_expolygon_2f(expoly);

    std::vector<Vec2f> out;
    if (! triangles.empty())
    {
        // Calculate area of each triangle.
        std::vector<float> areas;
        areas.reserve(triangles.size() / 3);
        for (size_t i = 0; i < triangles.size(); ) {
            const Vec2f &a  = triangles[i ++];
            const Vec2f  v1 = triangles[i ++] - a;
            const Vec2f  v2 = triangles[i ++] - a;
            areas.emplace_back(0.5f * std::abs(cross2(v1, v2)));
            if (i != 3)
                // Prefix sum of the areas.
                areas.back() += areas[areas.size() - 2];
        }

        size_t num_samples = size_t(ceil(areas.back() * samples_per_mm2));
        std::uniform_real_distribution<> random_triangle(0., double(areas.back()));
        std::uniform_real_distribution<> random_float(0., 1.);
        for (size_t i = 0; i < num_samples; ++ i) {
            double r = random_triangle(rng);
            size_t idx_triangle = std::min<size_t>(std::upper_bound(areas.begin(), areas.end(), (float)r) - areas.begin(), areas.size() - 1) * 3;
            // Select a random point on the triangle.
            double u = float(sqrt(random_float(rng)));
            double v = float(random_float(rng));
            const Vec2f &a = triangles[idx_triangle ++];
            const Vec2f &b = triangles[idx_triangle++];
            const Vec2f &c = triangles[idx_triangle];
            const Vec2f  x = a * (1.f - u) + b * (u * (1.f - v)) + c * (v * u);
            out.emplace_back(x);
        }
    }
    return out;
}

std::vector<Vec2f> sample_expolygon_with_boundary(const ExPolygon &expoly, float samples_per_mm2, float samples_per_mm_boundary, std::mt19937 &rng)
{
    std::vector<Vec2f> out = sample_expolygon(expoly, samples_per_mm2, rng);
    double             point_stepping_scaled = scale_(1.f) / samples_per_mm_boundary;
    for (size_t i_contour = 0; i_contour <= expoly.holes.size(); ++ i_contour) {
        const Polygon &contour = (i_contour == 0) ? expoly.contour : expoly.holes[i_contour - 1];
        const Points   pts = contour.equally_spaced_points(point_stepping_scaled);
        for (size_t i = 0; i < pts.size(); ++ i)
            out.emplace_back(unscale<float>(pts[i].x()), unscale<float>(pts[i].y()));
    }
    return out;
}

std::vector<Vec2f> poisson_disk_from_samples(const std::vector<Vec2f> &raw_samples, float radius)
{
    Vec2f corner_min(FLT_MAX, FLT_MAX);
    for (const Vec2f &pt : raw_samples) {
        corner_min.x() = std::min(corner_min.x(), pt.x());
        corner_min.y() = std::min(corner_min.y(), pt.y());
    }

    // Assign the raw samples to grid cells, sort the grid cells lexicographically.
    struct RawSample {
        Vec2f coord;
        Vec2i cell_id;
    };
    std::vector<RawSample> raw_samples_sorted;
    RawSample sample;
    for (const Vec2f &pt : raw_samples) {
        sample.coord   = pt;
        sample.cell_id = ((pt - corner_min) / radius).cast<int>();
        raw_samples_sorted.emplace_back(sample);
    }
    std::sort(raw_samples_sorted.begin(), raw_samples_sorted.end(), [](const RawSample &lhs, const RawSample &rhs) 
        { return lhs.cell_id.x() < rhs.cell_id.x() || (lhs.cell_id.x() == rhs.cell_id.x() && lhs.cell_id.y() < rhs.cell_id.y()); });

    struct PoissonDiskGridEntry {
        // Resulting output sample points for this cell:
        enum {
            max_positions = 4
        };
        Vec2f   poisson_samples[max_positions];
        int     num_poisson_samples = 0;

        // Index into raw_samples:
        int     first_sample_idx;
        int     sample_cnt;
    };

    struct CellIDHash {
        std::size_t operator()(const Vec2i &cell_id) {
            return std::hash<int>()(cell_id.x()) ^ std::hash<int>()(cell_id.y() * 593);
        }
    };

    // Map from cell IDs to hash_data.  Each hash_data points to the range in raw_samples corresponding to that cell.
    // (We could just store the samples in hash_data.  This implementation is an artifact of the reference paper, which
    // is optimizing for GPU acceleration that we haven't implemented currently.)
    typedef std::unordered_map<Vec2i, PoissonDiskGridEntry, CellIDHash> Cells;
    std::unordered_map<Vec2i, PoissonDiskGridEntry, CellIDHash> cells;
    {
        Cells::iterator last_cell_id_it;
        Vec2i           last_cell_id(-1, -1);
        for (int i = 0; i < raw_samples_sorted.size(); ++ i) {
            const RawSample &sample = raw_samples_sorted[i];
            if (sample.cell_id == last_cell_id) {
                // This sample is in the same cell as the previous, so just increase the count.  Cells are
                // always contiguous, since we've sorted raw_samples_sorted by cell ID.
                ++ last_cell_id_it->second.sample_cnt;
            } else {
                // This is a new cell.
                PoissonDiskGridEntry data;
                data.first_sample_idx = i;
                data.sample_cnt       = 1;
                auto result     = cells.insert({sample.cell_id, data});
                last_cell_id    = sample.cell_id;
                last_cell_id_it = result.first;
            }
        }
    }

    const int   max_trials = 5;
    const float radius_squared = radius * radius;
    for (int trial = 0; trial < max_trials; ++ trial) {
        // Create sample points for each entry in cells.
        for (auto &it : cells) {
            const Vec2i          &cell_id   = it.first;
            PoissonDiskGridEntry &cell_data = it.second;
            // This cell's raw sample points start at first_sample_idx.  On trial 0, try the first one. On trial 1, try first_sample_idx + 1.
            int next_sample_idx = cell_data.first_sample_idx + trial;
            if (trial >= cell_data.sample_cnt)
                // There are no more points to try for this cell.
                continue;
            const RawSample &candidate = raw_samples_sorted[next_sample_idx];
            // See if this point conflicts with any other points in this cell, or with any points in
            // neighboring cells.  Note that it's possible to have more than one point in the same cell.
            bool conflict = false;
            for (int i = -1; i < 2 && ! conflict; ++ i) {
                for (int j = -1; j < 2; ++ j) {
                    const auto &it_neighbor = cells.find(cell_id + Vec2i(i, j));
                    if (it_neighbor != cells.end()) {
                        const PoissonDiskGridEntry &neighbor = it_neighbor->second;
                        for (int i_sample = 0; i_sample < neighbor.num_poisson_samples; ++ i_sample)
                            if ((neighbor.poisson_samples[i_sample] - candidate.coord).squaredNorm() < radius_squared) {
                                conflict = true;
                                break;
                            }
                    }
                }
            }
            if (! conflict) {
                // Store the new sample.
                assert(cell_data.num_poisson_samples < cell_data.max_positions);
                if (cell_data.num_poisson_samples < cell_data.max_positions)
                    cell_data.poisson_samples[cell_data.num_poisson_samples ++] = candidate.coord;
            }
        }
    }

    // Copy the results to the output.
    std::vector<Vec2f> out;
    for (const auto it : cells)
        for (int i = 0; i < it.second.num_poisson_samples; ++ i)
            out.emplace_back(it.second.poisson_samples[i]);
    return out;
}

void SLAAutoSupports::uniformly_cover(const ExPolygon& island, Structure& structure, bool is_new_island, bool just_one)
{
    //int num_of_points = std::max(1, (int)((island.area()*pow(SCALING_FACTOR, 2) * m_config.tear_pressure)/m_config.support_force));

    const float density_horizontal = m_config.tear_pressure / m_config.support_force;
    //FIXME why?
    const float poisson_radius     = 1.f / (5.f * density_horizontal);
//    const float poisson_radius     = 1.f / (15.f * density_horizontal);
    const float samples_per_mm2    = 30.f / (float(M_PI) * poisson_radius * poisson_radius);

    //FIXME share the random generator. The random generator may be not so cheap to initialize, also we don't want the random generator to be restarted for each polygon.
    std::random_device  rd;
    std::mt19937        rng(rd());
	std::vector<Vec2f>  raw_samples = sample_expolygon_with_boundary(island, samples_per_mm2, 5.f / poisson_radius, rng);
    std::vector<Vec2f>  poisson_samples = poisson_disk_from_samples(raw_samples, poisson_radius);

#ifdef SLA_AUTOSUPPORTS_DEBUG
	{
		static int irun = 0;
		Slic3r::SVG svg(debug_out_path("SLA_supports-uniformly_cover-%d.svg", irun ++), get_extents(island));
		svg.draw(island);
		for (const Vec2f &pt : raw_samples)
			svg.draw(Point(scale_(pt.x()), scale_(pt.y())), "red");
		for (const Vec2f &pt : poisson_samples)
			svg.draw(Point(scale_(pt.x()), scale_(pt.y())), "blue");
	}
#endif /* NDEBUG */

    assert(! poisson_samples.empty());
    for (const Vec2f &pt : poisson_samples) {
        m_output.emplace_back(float(pt(0)), float(pt(1)), structure.height, 0.2f, is_new_island);
        structure.supports_force += m_config.support_force;
    }
}

#ifdef SLA_AUTOSUPPORTS_DEBUG
void SLAAutoSupports::output_structures(const std::vector<Structure>& structures)
{
    for (unsigned int i=0 ; i<structures.size(); ++i) {
        std::stringstream ss;
        ss << structures[i].unique_id.count() << "_" << std::setw(10) << std::setfill('0') << 1000 + (int)structures[i].height/1000 << ".png";
        output_expolygons(std::vector<ExPolygon>{*structures[i].polygon}, ss.str());
    }
}

void SLAAutoSupports::output_expolygons(const ExPolygons& expolys, const std::string &filename)
{
    BoundingBox bb(Point(-30000000, -30000000), Point(30000000, 30000000));
    Slic3r::SVG svg_cummulative(filename, bb);
    for (size_t i = 0; i < expolys.size(); ++ i) {
        /*Slic3r::SVG svg("single"+std::to_string(i)+".svg", bb);
        svg.draw(expolys[i]);
        svg.draw_outline(expolys[i].contour, "black", scale_(0.05));
        svg.draw_outline(expolys[i].holes, "blue", scale_(0.05));
        svg.Close();*/

        svg_cummulative.draw(expolys[i]);
        svg_cummulative.draw_outline(expolys[i].contour, "black", scale_(0.05));
        svg_cummulative.draw_outline(expolys[i].holes, "blue", scale_(0.05));
    }
}
#endif

} // namespace Slic3r
