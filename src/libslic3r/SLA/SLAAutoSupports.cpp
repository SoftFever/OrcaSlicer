#include "igl/random_points_on_mesh.h"
#include "igl/AABB.h"

#include "SLAAutoSupports.hpp"
#include "Model.hpp"
#include "ExPolygon.hpp"
#include "SVG.hpp"
#include "Point.hpp"
#include "ClipperUtils.hpp"

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
: m_config(config), m_V(emesh.V()), m_F(emesh.F()), m_throw_on_cancel(throw_on_cancel)
{
    process(slices, heights);
    project_onto_mesh(m_output);
}



void SLAAutoSupports::project_onto_mesh(std::vector<sla::SupportPoint>& points) const
{
    // The function  makes sure that all the points are really exactly placed on the mesh.
    igl::Hit hit_up{0, 0, 0.f, 0.f, 0.f};
    igl::Hit hit_down{0, 0, 0.f, 0.f, 0.f};
    
    for (sla::SupportPoint& support_point : points) {
        Vec3f& p = support_point.pos;
        // Project the point upward and downward and choose the closer intersection with the mesh.
        bool up   = igl::ray_mesh_intersect(p.cast<float>(), Vec3f(0., 0., 1.), m_V, m_F, hit_up);
        bool down = igl::ray_mesh_intersect(p.cast<float>(), Vec3f(0., 0., -1.), m_V, m_F, hit_down);

        if (!up && !down)
            continue;

        igl::Hit& hit = (!down || (hit_up.t < hit_down.t)) ? hit_up : hit_down;
        int fid = hit.id;
        Vec3f bc(1-hit.u-hit.v, hit.u, hit.v);
        p = (bc(0) * m_V.row(m_F(fid, 0)) + bc(1) * m_V.row(m_F(fid, 1)) + bc(2)*m_V.row(m_F(fid, 2))).cast<float>();
    }
}




void SLAAutoSupports::process(const std::vector<ExPolygons>& slices, const std::vector<float>& heights)
{
    std::vector<std::pair<ExPolygon, coord_t>> islands;

    for (unsigned int i = 0; i<slices.size(); ++i) {
        const ExPolygons& expolys_top = slices[i];

        const float height = (i>2 ? heights[i-3] : heights[0]-(heights[1]-heights[0]));
        const float layer_height = (i!=0 ? heights[i]-heights[i-1] : heights[0]);
        
        const float safe_angle = 5.f * (M_PI/180.f); // smaller number - less supports
        const float offset =  scale_(layer_height / std::tan(safe_angle));

        // FIXME: calculate actual pixel area from printer config:
        //const float pixel_area = pow(wxGetApp().preset_bundle->project_config.option<ConfigOptionFloat>("display_width") / wxGetApp().preset_bundle->project_config.option<ConfigOptionInt>("display_pixels_x"), 2.f); //
        const float pixel_area = pow(0.047f, 2.f);

        // Check all ExPolygons on this slice and check whether they are new or belonging to something below.
        for (const ExPolygon& polygon : expolys_top) {
            if (polygon.area() * SCALING_FACTOR * SCALING_FACTOR < pixel_area)
                continue;

            m_structures_new.emplace_back(polygon, height);
            for (Structure& s : m_structures_old) {
                const ExPolygon* bottom = s.polygon;
                if (polygon.overlaps(*bottom) || bottom->overlaps(polygon)) {
                    m_structures_new.back().structures_below.push_back(&s);

                    coord_t centroids_dist = (bottom->contour.centroid() - polygon.contour.centroid()).norm();
                    float mult = std::min(1.f, 1.f - std::min(1.f, (1600.f * layer_height) * (float)(centroids_dist * centroids_dist) / (float)bottom->area()));
                    s.supports_force *= mult;
                    s.supports_force *= std::min(1.f, 20.f * ((float)bottom->area() / (float)polygon.area()));
                }
            }
        }

        // Let's assign proper support force to each of them:
        for (Structure& old_str : m_structures_old) {
            std::vector<Structure*> children;
            float children_area = 0.f;
            for (Structure& new_str : m_structures_new)
                for (const Structure* below : new_str.structures_below)
                    if (&old_str == below) {
                        children.push_back(&new_str);
                        children_area += children.back()->polygon->area() * pow(SCALING_FACTOR, 2);
                    }
            for (Structure* child : children)
                child->supports_force += (old_str.supports_force/children_area) * (child->polygon->area() * pow(SCALING_FACTOR, 2));
        }
        
        // Now iterate over all polygons and append new points if needed.
        for (Structure& s : m_structures_new) {
            if (s.structures_below.empty()) {// completely new island - needs support no doubt
                uniformly_cover(*s.polygon, s, true);
            }
            else {
                // Let's see if there's anything that overlaps enough to need supports:
                ExPolygons polygons;
                float bottom_area = 0.f;
                for (const Structure* sb : s.structures_below) {
                    polygons.push_back(*sb->polygon);
                    bottom_area += polygons.back().area() * pow(SCALING_FACTOR, 2);
                }

                polygons = offset_ex(polygons, offset);
                polygons = diff_ex(ExPolygons{*s.polygon}, polygons);
                
                // What we now have in polygons needs support, regardless of what the forces are, so we can add them.
                for (const ExPolygon& p : polygons)
                    uniformly_cover(p, s);
            }
        }

        // We should also check if current support is enough given the polygon area.
        for (Structure& s : m_structures_new) {
            ExPolygons e;
            float e_area = 0.f;
            for (const Structure* a : s.structures_below) {
                e.push_back(*a->polygon);
                e_area += e.back().area() * SCALING_FACTOR * SCALING_FACTOR;
            }

            e = diff_ex(ExPolygons{*s.polygon}, e);
            s.supports_force /= std::max(1., (layer_height / 0.3f) * (e_area / (s.polygon->area()*SCALING_FACTOR*SCALING_FACTOR)));
                
                
                
            if ( (s.polygon->area() * pow(SCALING_FACTOR, 2)) * m_config.tear_pressure > s.supports_force) {
                ExPolygons::iterator largest_it = std::max_element(e.begin(), e.end(), [](const ExPolygon& a, const ExPolygon& b) { return a.area() < b.area(); });
                if (!e.empty())
                    uniformly_cover(*largest_it, s);
            }
        }

        // All is done. Prepare to advance to the next layer. 
        m_structures_old = m_structures_new;
        m_structures_new.clear();

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


void SLAAutoSupports::add_point(const Point& point, Structure& structure, bool is_new_island)
{
    sla::SupportPoint new_point(point(0) * SCALING_FACTOR, point(1) * SCALING_FACTOR, structure.height, 0.4f, (float)is_new_island);
    
    m_output.emplace_back(new_point);
    structure.supports_force += m_config.support_force;
}

void SLAAutoSupports::uniformly_cover(const ExPolygon& island, Structure& structure, bool is_new_island, bool just_one)
{
    //int num_of_points = std::max(1, (int)((island.area()*pow(SCALING_FACTOR, 2) * m_config.tear_pressure)/m_config.support_force));
    
    const float density_horizontal = m_config.tear_pressure / m_config.support_force;

    // We will cover the island another way.
    // For now we'll just place the points randomly not too close to the others.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0., 1.);

    std::vector<Vec3d> island_new_points;
    const BoundingBox& bb = get_extents(island);
    const int refused_limit = 30;
    int refused_points = 0;
    while (refused_points < refused_limit) {
        Point out;
        if (refused_points == 0 && island_new_points.empty()) // first iteration
            out = island.contour.centroid();
        else
            out = Point(bb.min(0) + bb.size()(0)  * dis(gen), bb.min(1) + bb.size()(1)  * dis(gen));

        Vec3d unscaled_out = unscale(out(0), out(1), 0.f);
        bool add_it = true;

        if (!island.contour.contains(out))
            add_it = false;
        else
            for (const Polygon& hole : island.holes)
                if (hole.contains(out))
                    add_it = false;

        if (add_it) {
            for (const Vec3d& p : island_new_points) {
                if ((p - unscaled_out).squaredNorm() < 1./(2.4*density_horizontal))    {
                    add_it = false;
                    break;
                }
            }
        }
        if (add_it) {
            island_new_points.emplace_back(unscaled_out);
            if (just_one)
                break;
        }
        else
            ++refused_points;
    }

    for (const Vec3d& p : island_new_points)
        add_point(Point(scale_(p.x()), scale_(p.y())), structure, is_new_island);
}

#ifdef SLA_AUTOSUPPORTS_DEBUG
void SLAAutoSupports::output_structures() const
{
    const std::vector<Structure>& structures = m_structures_new;
    for (unsigned int i=0 ; i<structures.size(); ++i) {
        std::stringstream ss;
        ss << structures[i].unique_id.count() << "_" << std::setw(10) << std::setfill('0') << 1000 + (int)structures[i].height/1000 << ".png";
        output_expolygons(std::vector<ExPolygon>{*structures[i].polygon}, ss.str());
    }
}

void SLAAutoSupports::output_expolygons(const ExPolygons& expolys, std::string filename) const
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
