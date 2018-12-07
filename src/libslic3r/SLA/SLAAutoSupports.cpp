#include "igl/random_points_on_mesh.h"
#include "igl/AABB.h"

#include "SLAAutoSupports.hpp"
#include "Model.hpp"

#include <iostream>


namespace Slic3r {

SLAAutoSupports::SLAAutoSupports(ModelObject& mo, const SLAAutoSupports::Config& c)
: m_model_object(mo), mesh(), m_config(c)
{}


float SLAAutoSupports::approximate_geodesic_distance(const Vec3f& p1, const Vec3f& p2, Vec3f& n1, Vec3f& n2)
{
    n1.normalize();
    n2.normalize();

    Vec3f v = (p2-p1);
    v.normalize();

    float c1 = n1.dot(v);
    float c2 = n2.dot(v);
    float result = pow(p1(0)-p2(0), 2) + pow(p1(1)-p2(1), 2) + pow(p1(2)-p2(2), 2);
    // Check for division by zero:
    if(fabs(c1 - c2) > 0.0001)
        result *= (asin(c1) - asin(c2)) / (c1 - c2);
    return result;
}


void SLAAutoSupports::generate()
{
    // Loads the ModelObject raw_mesh and transforms it by first instance's transformation matrix (disregarding translation).
    // Instances only differ in z-rotation, so it does not matter which of them will be used for the calculation.
    // The supports point will be calculated on this mesh (so scaling ang vertical direction is correctly accounted for).
    // Results will be inverse-transformed to raw_mesh coordinates.
    TriangleMesh mesh = m_model_object.raw_mesh();
    Transform3d transformation_matrix = m_model_object.instances[0]->get_matrix(true/*dont_translate*/);
    mesh.transform(transformation_matrix);

    // Check that the object is thick enough to produce any support points
    BoundingBoxf3 bb = mesh.bounding_box();
    if (bb.size()(2) < m_config.minimal_z)
        return;

    // All points that we curretly have must be transformed too, so distance to them is correcly calculated.
    for (Vec3f& point : m_model_object.sla_support_points)
        point = transformation_matrix.cast<float>() * point;

    const stl_file& stl = mesh.stl;
    Eigen::MatrixXf V;
    Eigen::MatrixXi F;
    V.resize(3 * stl.stats.number_of_facets, 3);
    F.resize(stl.stats.number_of_facets, 3);
    for (unsigned int i=0; i<stl.stats.number_of_facets; ++i) {
        const stl_facet* facet = stl.facet_start+i;
        V(3*i+0, 0) = facet->vertex[0](0); V(3*i+0, 1) = facet->vertex[0](1); V(3*i+0, 2) = facet->vertex[0](2);
        V(3*i+1, 0) = facet->vertex[1](0); V(3*i+1, 1) = facet->vertex[1](1); V(3*i+1, 2) = facet->vertex[1](2);
        V(3*i+2, 0) = facet->vertex[2](0); V(3*i+2, 1) = facet->vertex[2](1); V(3*i+2, 2) = facet->vertex[2](2);
        F(i, 0) = 3*i+0;
        F(i, 1) = 3*i+1;
        F(i, 2) = 3*i+2;
    }

    // In order to calculate distance to already placed points, we must keep know which facet the point lies on.
    std::vector<Vec3f> facets_normals;

    // The AABB hierarchy will be used to find normals of already placed points.
    // The points added automatically will just push_back the new normal on the fly.
    igl::AABB<Eigen::MatrixXf,3> aabb;
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
    }

    // New potential support point is randomly generated on the mesh and distance to all already placed points is calculated.
    // In case it is never smaller than certain limit (depends on the new point's facet normal), the point is accepted.
    // The process stops after certain number of points is refused in a row.
    Vec3f point;
    Vec3f normal;
    int added_points = 0;
    int refused_points = 0;
    const int refused_limit = 30;
    // Angle at which the density reaches zero:
    const float threshold_angle = std::min(M_PI_2, M_PI_4 * acos(0.f/m_config.density_at_horizontal) / acos(m_config.density_at_45/m_config.density_at_horizontal));

    srand(time(NULL)); // rand() is used by igl::random_point_on_mesh
    while (refused_points < refused_limit) {
        // Place a random point on the mesh and calculate corresponding facet's normal:
        Eigen::VectorXi FI;
        Eigen::MatrixXf B;
        igl::random_points_on_mesh(1, V, F, B, FI);
        point = B(0,0)*V.row(F(FI(0),0)) +
                B(0,1)*V.row(F(FI(0),1)) +
                B(0,2)*V.row(F(FI(0),2));
        if (point(2) - bb.min(2) < m_config.minimal_z)
            continue;

        Vec3f a1 = V.row(F(FI(0),1)) - V.row(F(FI(0),0));
        Vec3f a2 = V.row(F(FI(0),2)) - V.row(F(FI(0),0));
        normal = a1.cross(a2);
        normal.normalize();

        // calculate angle between the normal and vertical:
        float angle = angle_from_normal(normal);
        if (angle > threshold_angle)
            continue;

        const float distance_limit = 1./(2.4*get_required_density(angle));
        bool add_it = true;
        for (unsigned int i=0; i<m_model_object.sla_support_points.size(); ++i) {
            if (approximate_geodesic_distance(m_model_object.sla_support_points[i], point, facets_normals[i], normal) < distance_limit) {
                add_it = false;
                ++refused_points;
                break;
            }
        }
        if (add_it) {
            m_model_object.sla_support_points.push_back(point);
            facets_normals.push_back(normal);
            ++added_points;
            refused_points = 0;
        }
    }

    // Now transform all support points to mesh coordinates:
    for (Vec3f& point : m_model_object.sla_support_points)
        point = transformation_matrix.inverse().cast<float>() * point;
}



float SLAAutoSupports::get_required_density(float angle) const
{
    // calculation would be density_0 * cos(angle). To provide one more degree of freedom, we will scale the angle
    // to get the user-set density for 45 deg. So it ends up as density_0 * cos(K * angle).
    float K = 4.f * float(acos(m_config.density_at_45/m_config.density_at_horizontal) / M_PI);
    return std::max(0.f, float(m_config.density_at_horizontal * cos(K*angle)));
}



} // namespace Slic3r