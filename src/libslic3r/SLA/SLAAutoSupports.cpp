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

SLAAutoSupports::SLAAutoSupports(const TriangleMesh& mesh, const sla::EigenMesh3D& emesh, const std::vector<ExPolygons>& slices, const std::vector<float>& heights, 
    const Config& config, std::function<void(void)> throw_on_cancel)
: m_config(config), m_V(emesh.V), m_F(emesh.F), m_throw_on_cancel(throw_on_cancel)
{
    // FIXME: It might be safer to get rid of the rand() calls altogether, because it is probably
    // not always thread-safe and can be slow if it is.
    srand(time(NULL)); // rand() is used by igl::random_point_on_mesh

    // Find all separate islands that will need support. The coord_t number denotes height
    // of a point just below the mesh (so that we can later project the point precisely
    // on the mesh by raycasting (done by igl) and not risking we will place the point inside).
    std::vector<std::pair<ExPolygon, coord_t>> islands = find_islands(slices, heights);

    // Uniformly cover each of the islands with support points.
    for (const auto& island : islands) {
        std::vector<Vec3d> points = uniformly_cover(island);
        m_throw_on_cancel();
        project_upward_onto_mesh(points);
        m_output.insert(m_output.end(), points.begin(), points.end());
        m_throw_on_cancel();
    }

    // We are done with the islands. Let's sprinkle the rest of the mesh.
    // The function appends to m_output.
    sprinkle_mesh(mesh);
}


float SLAAutoSupports::approximate_geodesic_distance(const Vec3d& p1, const Vec3d& p2, Vec3d& n1, Vec3d& n2)
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


void SLAAutoSupports::sprinkle_mesh(const TriangleMesh& mesh)
{
    std::vector<Vec3d> points;
    // Loads the ModelObject raw_mesh and transforms it by first instance's transformation matrix (disregarding translation).
    // Instances only differ in z-rotation, so it does not matter which of them will be used for the calculation.
    // The supports point will be calculated on this mesh (so scaling ang vertical direction is correctly accounted for).
    // Results will be inverse-transformed to raw_mesh coordinates.
    //TriangleMesh mesh = m_model_object.raw_mesh();
    //Transform3d transformation_matrix = m_model_object.instances[0]->get_matrix(true/*dont_translate*/);
    //mesh.transform(transformation_matrix);

    // Check that the object is thick enough to produce any support points
    BoundingBoxf3 bb = mesh.bounding_box();
    if (bb.size()(2) < m_config.minimal_z)
        return;

    // All points that we curretly have must be transformed too, so distance to them is correcly calculated.
    //for (Vec3f& point : m_model_object.sla_support_points)
    //    point = transformation_matrix.cast<float>() * point;


    // In order to calculate distance to already placed points, we must keep know which facet the point lies on.
    std::vector<Vec3d> facets_normals;

    // Only points belonging to islands were added so far - they all lie on horizontal surfaces:
    for (unsigned int i=0; i<m_output.size(); ++i)
        facets_normals.push_back(Vec3d(0,0,-1));

    // The AABB hierarchy will be used to find normals of already placed points.
    // The points added automatically will just push_back the new normal on the fly.
    /*igl::AABB<Eigen::MatrixXf,3> aabb;
    aabb.init(V, F);
    for (unsigned int i=0; i<m_model_object.sla_support_points.size(); ++i) {
        int facet_idx = 0;
        Eigen::Matrix<float, 1, 3> dump;
        Eigen::MatrixXf query_point = m_model_object.sla_support_points[i];
        aabb.squared_distance(V, F, query_point, facet_idx, dump);
        Vec3f a1 = V.row(F(facet_idx,1)) - V.row(F(facet_idx,0));
        Vec3f a2 = V.row(F(facet_idx,2)) - V.row(F(facet_idx,0));
        Vec3f normal = a1.cross(a2);
        normal.normalize();
        facets_normals.push_back(normal);
    }*/

    // New potential support point is randomly generated on the mesh and distance to all already placed points is calculated.
    // In case it is never smaller than certain limit (depends on the new point's facet normal), the point is accepted.
    // The process stops after certain number of points is refused in a row.
    Vec3d point;
    Vec3d normal;
    int added_points = 0;
    int refused_points = 0;
    const int refused_limit = 30;
    // Angle at which the density reaches zero:
    const float threshold_angle = std::min(M_PI_2, M_PI_4 * acos(0.f/m_config.density_at_horizontal) / acos(m_config.density_at_45/m_config.density_at_horizontal));

    size_t cancel_test_cntr = 0;
    while (refused_points < refused_limit) {
        if (++ cancel_test_cntr == 500) {
            // Don't call the cancellation routine too often as the multi-core cache synchronization
            // may be pretty expensive.
            m_throw_on_cancel();
            cancel_test_cntr = 0;
        }
        // Place a random point on the mesh and calculate corresponding facet's normal:
        Eigen::VectorXi FI;
        Eigen::MatrixXd B;
        igl::random_points_on_mesh(1, m_V, m_F, B, FI);
        point = B(0,0)*m_V.row(m_F(FI(0),0)) +
                B(0,1)*m_V.row(m_F(FI(0),1)) +
                B(0,2)*m_V.row(m_F(FI(0),2));
        if (point(2) - bb.min(2) < m_config.minimal_z)
            continue;

        Vec3d a1 = m_V.row(m_F(FI(0),1)) - m_V.row(m_F(FI(0),0));
        Vec3d a2 = m_V.row(m_F(FI(0),2)) - m_V.row(m_F(FI(0),0));
        normal = a1.cross(a2);
        normal.normalize();

        // calculate angle between the normal and vertical:
        float angle = angle_from_normal(normal.cast<float>());
        if (angle > threshold_angle)
            continue;

        const float limit = distance_limit(angle);
        bool add_it = true;
        for (unsigned int i=0; i<points.size(); ++i) {
            if (approximate_geodesic_distance(points[i], point, facets_normals[i], normal) < limit) {
                add_it = false;
                ++refused_points;
                break;
            }
        }
        if (add_it) {
            points.push_back(point.cast<double>());
            facets_normals.push_back(normal);
            ++added_points;
            refused_points = 0;
        }
    }

    m_output.insert(m_output.end(), points.begin(), points.end());

    // Now transform all support points to mesh coordinates:
    //for (Vec3f& point : m_model_object.sla_support_points)
    //    point = transformation_matrix.inverse().cast<float>() * point;
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
}

#ifdef SLA_AUTOSUPPORTS_DEBUG
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
#endif /* SLA_AUTOSUPPORTS_DEBUG */

std::vector<std::pair<ExPolygon, coord_t>> SLAAutoSupports::find_islands(const std::vector<ExPolygons>& slices, const std::vector<float>& heights) const
{
    std::vector<std::pair<ExPolygon, coord_t>> islands;

    struct PointAccessor {
        const Point* operator()(const Point &pt) const { return &pt; }
    };
    typedef ClosestPointInRadiusLookup<Point, PointAccessor> ClosestPointLookupType;

    for (unsigned int i = 0; i<slices.size(); ++i) {
        const ExPolygons& expolys_top = slices[i];
        const ExPolygons& expolys_bottom = (i == 0 ? ExPolygons() : slices[i-1]);

        std::string layer_num_str = std::string((i<10 ? "0" : "")) + std::string((i<100 ? "0" : "")) + std::to_string(i);
#ifdef SLA_AUTOSUPPORTS_DEBUG
        output_expolygons(expolys_top, "top" + layer_num_str + ".svg");
#endif /* SLA_AUTOSUPPORTS_DEBUG */
        ExPolygons diff = diff_ex(expolys_top, expolys_bottom);

#ifdef SLA_AUTOSUPPORTS_DEBUG
        output_expolygons(diff, "diff" + layer_num_str + ".svg");
#endif /* SLA_AUTOSUPPORTS_DEBUG */

        ClosestPointLookupType cpl(SCALED_EPSILON);
        for (const ExPolygon& expol : expolys_top) {
            for (const Point& p : expol.contour.points)
                cpl.insert(p);
            for (const Polygon& hole : expol.holes)
                for (const Point& p : hole.points)
                    cpl.insert(p);
            // the lookup structure now contains all points from the top slice
        }

        for (const ExPolygon& polygon : diff) {
            // we want to check all boundary points of the diff polygon
            bool island = true;
            for (const Point& p : polygon.contour.points) {
                if (cpl.find(p).second != 0) { // the point belongs to the bottom slice - this cannot be an island
                    island = false;
                    goto NO_ISLAND;
                }
            }
            for (const Polygon& hole : polygon.holes)
                for (const Point& p : hole.points)
                if (cpl.find(p).second != 0) {
                    island = false;
                    goto NO_ISLAND;
                }

            if (island) { // all points of the diff polygon are from the top slice
                islands.push_back(std::make_pair(polygon, scale_(i!=0 ? heights[i-1] : heights[0]-(heights[1]-heights[0]))));
            }
            NO_ISLAND: ;// continue with next ExPolygon
        }

#ifdef SLA_AUTOSUPPORTS_DEBUG
        //if (!islands.empty())
          //  output_expolygons(islands, "islands" + layer_num_str + ".svg");
#endif /* SLA_AUTOSUPPORTS_DEBUG */
        m_throw_on_cancel();
    }

    return islands;
}

std::vector<Vec3d> SLAAutoSupports::uniformly_cover(const std::pair<ExPolygon, coord_t>& island)
{
    int num_of_points = std::max(1, (int)(island.first.area()*pow(SCALING_FACTOR, 2) * get_required_density(0)));

    // In case there is just one point to place, we'll place it into the polygon's centroid (unless it lies in a hole).
    if (num_of_points == 1) {
        Point out(island.first.contour.centroid());

        for (const auto& hole : island.first.holes)
            if (hole.contains(out))
                goto HOLE_HIT;
        return std::vector<Vec3d>{unscale(out(0), out(1), island.second)};
    }

HOLE_HIT:
    // In this case either the centroid lies in a hole, or there are multiple points
    // to place. We will cover the island another way.
    // For now we'll just place the points randomly not too close to the others.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0., 1.);

    std::vector<Vec3d> island_new_points;
    const BoundingBox& bb = get_extents(island.first);
    const int refused_limit = 30;
    int refused_points = 0;
    while (refused_points < refused_limit) {
        Point out(bb.min(0) + bb.size()(0)  * dis(gen),
                  bb.min(1) + bb.size()(1)  * dis(gen)) ;
        Vec3d unscaled_out = unscale(out(0), out(1), island.second);
        bool add_it = true;

        if (!island.first.contour.contains(out))
            add_it = false;
        else
            for (const Polygon& hole : island.first.holes)
                if (hole.contains(out))
                    add_it = false;

        if (add_it) {
            for (const Vec3d& p : island_new_points) {
                if ((p - unscaled_out).squaredNorm() < distance_limit(0)) {
                    add_it = false;
                    ++refused_points;
                    break;
                }
            }
        }
        if (add_it)
            island_new_points.emplace_back(unscaled_out);
    }
    return island_new_points;
}

void SLAAutoSupports::project_upward_onto_mesh(std::vector<Vec3d>& points) const
{
    Vec3f dir(0., 0., 1.);
    igl::Hit hit{0, 0, 0.f, 0.f, 0.f};
    for (Vec3d& p : points) {
        igl::ray_mesh_intersect(p.cast<float>(), dir, m_V, m_F, hit);
        int fid = hit.id;
        Vec3f bc(1-hit.u-hit.v, hit.u, hit.v);
        p = (bc(0) * m_V.row(m_F(fid, 0)) + bc(1) * m_V.row(m_F(fid, 1)) + bc(2)*m_V.row(m_F(fid, 2))).cast<double>();
    }
}


} // namespace Slic3r