#include "Exception.hpp"
#include "TriangleMesh.hpp"
#include "TriangleMeshSlicer.hpp"
#include "MeshSplitImpl.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "Point.hpp"
#include "Execution/ExecutionTBB.hpp"
#include "Execution/ExecutionSeq.hpp"

#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullVertexSet.h>

#include <cmath>
#include <deque>
#include <queue>
#include <vector>
#include <utility>
#include <algorithm>
#include <type_traits>

#include <boost/log/trivial.hpp>

#include <Eigen/Core>
#include <Eigen/Dense>

#include <assert.h>

namespace Slic3r {

TriangleMesh::TriangleMesh(const Pointf3s &points, const std::vector<Vec3i> &facets) : repaired(false)
{
    stl_file &stl = this->stl;
    stl.stats.type = inmemory;

    // count facets and allocate memory
    stl.stats.number_of_facets = (uint32_t)facets.size();
    stl.stats.original_num_facets = stl.stats.number_of_facets;
    stl_allocate(&stl);

    for (uint32_t i = 0; i < stl.stats.number_of_facets; ++ i) {
        stl_facet facet;
        facet.vertex[0] = points[facets[i](0)].cast<float>();
        facet.vertex[1] = points[facets[i](1)].cast<float>();
        facet.vertex[2] = points[facets[i](2)].cast<float>();
        facet.extra[0] = 0;
        facet.extra[1] = 0;

        stl_normal normal;
        stl_calculate_normal(normal, &facet);
        stl_normalize_vector(normal);
        facet.normal = normal;

        stl.facet_start[i] = facet;
    }
    stl_get_size(&stl);
}

TriangleMesh::TriangleMesh(const indexed_triangle_set &M) : repaired(false)
{
    stl.stats.type = inmemory;
    
    // count facets and allocate memory
    stl.stats.number_of_facets = uint32_t(M.indices.size());
    stl.stats.original_num_facets = int(stl.stats.number_of_facets);
    stl_allocate(&stl);
    
    for (uint32_t i = 0; i < stl.stats.number_of_facets; ++ i) {
        stl_facet facet;
        facet.vertex[0] = M.vertices[size_t(M.indices[i](0))];
        facet.vertex[1] = M.vertices[size_t(M.indices[i](1))];
        facet.vertex[2] = M.vertices[size_t(M.indices[i](2))];
        facet.extra[0] = 0;
        facet.extra[1] = 0;
        
        stl_normal normal;
        stl_calculate_normal(normal, &facet);
        stl_normalize_vector(normal);
        facet.normal = normal;
        
        stl.facet_start[i] = facet;
    }
    
    stl_get_size(&stl);
}

// #define SLIC3R_TRACE_REPAIR

void TriangleMesh::repair(bool update_shared_vertices)
{
    if (this->repaired) {
        if (update_shared_vertices)
            this->require_shared_vertices();
        return;
    }

    // admesh fails when repairing empty meshes
    if (this->stl.stats.number_of_facets == 0)
        return;

    BOOST_LOG_TRIVIAL(debug) << "TriangleMesh::repair() started";

    // checking exact
#ifdef SLIC3R_TRACE_REPAIR
    BOOST_LOG_TRIVIAL(trace) << "\tstl_check_faces_exact";
#endif /* SLIC3R_TRACE_REPAIR */
    assert(stl_validate(&this->stl));
    stl_check_facets_exact(&stl);
    assert(stl_validate(&this->stl));
    stl.stats.facets_w_1_bad_edge = (stl.stats.connected_facets_2_edge - stl.stats.connected_facets_3_edge);
    stl.stats.facets_w_2_bad_edge = (stl.stats.connected_facets_1_edge - stl.stats.connected_facets_2_edge);
    stl.stats.facets_w_3_bad_edge = (stl.stats.number_of_facets - stl.stats.connected_facets_1_edge);
    
    // checking nearby
    //int last_edges_fixed = 0;
    float tolerance = (float)stl.stats.shortest_edge;
    float increment = (float)stl.stats.bounding_diameter / 10000.0f;
    int iterations = 2;
    if (stl.stats.connected_facets_3_edge < (int)stl.stats.number_of_facets) {
        for (int i = 0; i < iterations; i++) {
            if (stl.stats.connected_facets_3_edge < (int)stl.stats.number_of_facets) {
                //printf("Checking nearby. Tolerance= %f Iteration=%d of %d...", tolerance, i + 1, iterations);
#ifdef SLIC3R_TRACE_REPAIR
                BOOST_LOG_TRIVIAL(trace) << "\tstl_check_faces_nearby";
#endif /* SLIC3R_TRACE_REPAIR */
                stl_check_facets_nearby(&stl, tolerance);
                //printf("  Fixed %d edges.\n", stl.stats.edges_fixed - last_edges_fixed);
                //last_edges_fixed = stl.stats.edges_fixed;
                tolerance += increment;
            } else {
                break;
            }
        }
    }
    assert(stl_validate(&this->stl));
    
    // remove_unconnected
    if (stl.stats.connected_facets_3_edge < (int)stl.stats.number_of_facets) {
#ifdef SLIC3R_TRACE_REPAIR
        BOOST_LOG_TRIVIAL(trace) << "\tstl_remove_unconnected_facets";
#endif /* SLIC3R_TRACE_REPAIR */
        stl_remove_unconnected_facets(&stl);
        assert(stl_validate(&this->stl));
    }
    
    // fill_holes
#if 0
    // Don't fill holes, the current algorithm does more harm than good on complex holes.
    // Rather let the slicing algorithm close gaps in 2D slices.
    if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
#ifdef SLIC3R_TRACE_REPAIR
        BOOST_LOG_TRIVIAL(trace) << "\tstl_fill_holes";
#endif /* SLIC3R_TRACE_REPAIR */
        stl_fill_holes(&stl);
        stl_clear_error(&stl);
    }
#endif

    // normal_directions
#ifdef SLIC3R_TRACE_REPAIR
    BOOST_LOG_TRIVIAL(trace) << "\tstl_fix_normal_directions";
#endif /* SLIC3R_TRACE_REPAIR */
    stl_fix_normal_directions(&stl);
    assert(stl_validate(&this->stl));

    // normal_values
#ifdef SLIC3R_TRACE_REPAIR
    BOOST_LOG_TRIVIAL(trace) << "\tstl_fix_normal_values";
#endif /* SLIC3R_TRACE_REPAIR */
    stl_fix_normal_values(&stl);
    assert(stl_validate(&this->stl));
    
    // always calculate the volume and reverse all normals if volume is negative
#ifdef SLIC3R_TRACE_REPAIR
    BOOST_LOG_TRIVIAL(trace) << "\tstl_calculate_volume";
#endif /* SLIC3R_TRACE_REPAIR */
    stl_calculate_volume(&stl);
    assert(stl_validate(&this->stl));
    
    // neighbors
#ifdef SLIC3R_TRACE_REPAIR
    BOOST_LOG_TRIVIAL(trace) << "\tstl_verify_neighbors";
#endif /* SLIC3R_TRACE_REPAIR */
    stl_verify_neighbors(&stl);
    assert(stl_validate(&this->stl));

    this->repaired = true;

    BOOST_LOG_TRIVIAL(debug) << "TriangleMesh::repair() finished";

    // This call should be quite cheap, a lot of code requires the indexed_triangle_set data structure,
    // and it is risky to generate such a structure once the meshes are shared. Do it now.
    this->its.clear();
    if (update_shared_vertices)
        this->require_shared_vertices();
}

float TriangleMesh::volume()
{
    if (this->stl.stats.volume == -1) 
        stl_calculate_volume(&this->stl);
    return this->stl.stats.volume;
}

void TriangleMesh::check_topology()
{
    // checking exact
    stl_check_facets_exact(&stl);
    stl.stats.facets_w_1_bad_edge = (stl.stats.connected_facets_2_edge - stl.stats.connected_facets_3_edge);
    stl.stats.facets_w_2_bad_edge = (stl.stats.connected_facets_1_edge - stl.stats.connected_facets_2_edge);
    stl.stats.facets_w_3_bad_edge = (stl.stats.number_of_facets - stl.stats.connected_facets_1_edge);
    
    // checking nearby
    //int last_edges_fixed = 0;
    float tolerance = stl.stats.shortest_edge;
    float increment = stl.stats.bounding_diameter / 10000.0;
    int iterations = 2;
    if (stl.stats.connected_facets_3_edge < (int)stl.stats.number_of_facets) {
        for (int i = 0; i < iterations; i++) {
            if (stl.stats.connected_facets_3_edge < (int)stl.stats.number_of_facets) {
                //printf("Checking nearby. Tolerance= %f Iteration=%d of %d...", tolerance, i + 1, iterations);
                stl_check_facets_nearby(&stl, tolerance);
                //printf("  Fixed %d edges.\n", stl.stats.edges_fixed - last_edges_fixed);
                //last_edges_fixed = stl.stats.edges_fixed;
                tolerance += increment;
            } else {
                break;
            }
        }
    }
}

void TriangleMesh::reset_repair_stats() {
    this->stl.stats.degenerate_facets   = 0;
    this->stl.stats.edges_fixed         = 0;
    this->stl.stats.facets_removed      = 0;
    this->stl.stats.facets_added        = 0;
    this->stl.stats.facets_reversed     = 0;
    this->stl.stats.backwards_edges     = 0;
    this->stl.stats.normals_fixed       = 0;
}

bool TriangleMesh::needed_repair() const
{
    return this->stl.stats.degenerate_facets    > 0
        || this->stl.stats.edges_fixed          > 0
        || this->stl.stats.facets_removed       > 0
        || this->stl.stats.facets_added         > 0
        || this->stl.stats.facets_reversed      > 0
        || this->stl.stats.backwards_edges      > 0;
}

void TriangleMesh::WriteOBJFile(const char* output_file) const
{
    its_write_obj(this->its, output_file);
}

void TriangleMesh::scale(float factor)
{
    stl_scale(&(this->stl), factor);
    for (stl_vertex& v : this->its.vertices)
        v *= factor;
}

void TriangleMesh::scale(const Vec3d &versor)
{
    stl_scale_versor(&this->stl, versor.cast<float>());
    for (stl_vertex& v : this->its.vertices) {
        v.x() *= versor.x();
        v.y() *= versor.y();
        v.z() *= versor.z();
    }
}

void TriangleMesh::translate(float x, float y, float z)
{
    if (x == 0.f && y == 0.f && z == 0.f)
        return;
    stl_translate_relative(&(this->stl), x, y, z);
    stl_vertex shift(x, y, z);
    for (stl_vertex& v : this->its.vertices)
        v += shift;
}

void TriangleMesh::translate(const Vec3f &displacement)
{
    translate(displacement(0), displacement(1), displacement(2));
}

void TriangleMesh::rotate(float angle, const Axis &axis)
{
    if (angle == 0.f)
        return;

    // admesh uses degrees
    angle = Slic3r::Geometry::rad2deg(angle);
    
    if (axis == X) {
        stl_rotate_x(&this->stl, angle);
        its_rotate_x(this->its, angle);
    } else if (axis == Y) {
        stl_rotate_y(&this->stl, angle);
        its_rotate_y(this->its, angle);
    } else if (axis == Z) {
        stl_rotate_z(&this->stl, angle);
        its_rotate_z(this->its, angle);
    }
}

void TriangleMesh::rotate(float angle, const Vec3d& axis)
{
    if (angle == 0.f)
        return;

    Vec3d axis_norm = axis.normalized();
    Transform3d m = Transform3d::Identity();
    m.rotate(Eigen::AngleAxisd(angle, axis_norm));
    stl_transform(&stl, m);
    its_transform(its, m);
}

void TriangleMesh::mirror(const Axis &axis)
{
    if (axis == X) {
        stl_mirror_yz(&this->stl);
        for (stl_vertex &v : this->its.vertices)
            v(0) *= -1.0;
    } else if (axis == Y) {
        stl_mirror_xz(&this->stl);
        for (stl_vertex &v : this->its.vertices)
            v(1) *= -1.0;
    } else if (axis == Z) {
        stl_mirror_xy(&this->stl);
        for (stl_vertex &v : this->its.vertices)
            v(2) *= -1.0;
    }
}

void TriangleMesh::transform(const Transform3d& t, bool fix_left_handed)
{
    stl_transform(&stl, t);
    its_transform(its, t);
    if (fix_left_handed && t.matrix().block(0, 0, 3, 3).determinant() < 0.) {
        // Left handed transformation is being applied. It is a good idea to flip the faces and their normals.
        // As for the assert: the repair function would fix the normals, reversing would
        // break them again. The caller should provide a mesh that does not need repair.
        // The repair call is left here so things don't break more than they were.
        assert(this->repaired);
        this->repair(false);
        stl_reverse_all_facets(&stl);
        this->its.clear();
        this->require_shared_vertices();
    }
}

void TriangleMesh::transform(const Matrix3d& m, bool fix_left_handed)
{
    stl_transform(&stl, m);
    its_transform(its, m);
    if (fix_left_handed && m.determinant() < 0.) {
        // See comments in function above.
        assert(this->repaired);
        this->repair(false);
        stl_reverse_all_facets(&stl);
        this->its.clear();
        this->require_shared_vertices();
    }
}

void TriangleMesh::align_to_origin()
{
    this->translate(
        - this->stl.stats.min(0),
        - this->stl.stats.min(1),
        - this->stl.stats.min(2));
}

void TriangleMesh::rotate(double angle, Point* center)
{
    if (angle == 0.)
        return;
    Vec2f c = center->cast<float>();
    this->translate(-c(0), -c(1), 0);
    stl_rotate_z(&this->stl, (float)angle);
    its_rotate_z(this->its, (float)angle);
    this->translate(c(0), c(1), 0);
}

/**
 * Calculates whether or not the mesh is splittable.
 */
bool TriangleMesh::is_splittable() const
{
    std::vector<unsigned char> visited;
    find_unvisited_neighbors(visited);

    // Try finding an unvisited facet. If there are none, the mesh is not splittable.
    auto it = std::find(visited.begin(), visited.end(), false);
    return it != visited.end();
}

/**
 * Visit all unvisited neighboring facets that are reachable from the first unvisited facet,
 * and return them.
 * 
 * @param facet_visited A reference to a vector of booleans. Contains whether or not a
 *                      facet with the same index has been visited.
 * @return A deque with all newly visited facets.
 */
std::deque<uint32_t> TriangleMesh::find_unvisited_neighbors(std::vector<unsigned char> &facet_visited) const
{
    // Make sure we're not operating on a broken mesh.
    if (!this->repaired)
        throw Slic3r::RuntimeError("find_unvisited_neighbors() requires repair()");

    // If the visited list is empty, populate it with false for every facet.
    if (facet_visited.empty())
        facet_visited = std::vector<unsigned char>(this->stl.stats.number_of_facets, false);

    // Find the first unvisited facet.
    std::queue<uint32_t> facet_queue;
    std::deque<uint32_t> facets;
    auto facet = std::find(facet_visited.begin(), facet_visited.end(), false);
    if (facet != facet_visited.end()) {
        uint32_t idx = uint32_t(facet - facet_visited.begin());
        facet_queue.push(idx);
        facet_visited[idx] = true;
        facets.emplace_back(idx);
    }

    // Traverse all reachable neighbors and mark them as visited.
    while (! facet_queue.empty()) {
        uint32_t facet_idx = facet_queue.front();
        facet_queue.pop();
        for (int neighbor_idx : this->stl.neighbors_start[facet_idx].neighbor)
            if (neighbor_idx != -1 && ! facet_visited[neighbor_idx]) {
                facet_queue.push(uint32_t(neighbor_idx));
                facet_visited[neighbor_idx] = true;
                facets.emplace_back(uint32_t(neighbor_idx));
            }
    }

    return facets;
}

/**
 * Splits a mesh into multiple meshes when possible.
 * 
 * @return A TriangleMeshPtrs with the newly created meshes.
 */
TriangleMeshPtrs TriangleMesh::split() const
{
    struct MeshAdder {
        TriangleMeshPtrs &meshes;
        MeshAdder(TriangleMeshPtrs &ptrs): meshes{ptrs} {}
        void operator=(const indexed_triangle_set &its)
        {
            meshes.emplace_back(new TriangleMesh(its));
        }
    };

    TriangleMeshPtrs meshes;
    if (has_shared_vertices()) {
        its_split(its, MeshAdder{meshes});
    } else {
        // Loop while we have remaining facets.
        std::vector<unsigned char> facet_visited;
        for (;;) {
            std::deque<uint32_t> facets = find_unvisited_neighbors(facet_visited);
            if (facets.empty())
                break;

            // Create a new mesh for the part that was just split off.
            TriangleMesh* mesh = new TriangleMesh;
            meshes.emplace_back(mesh);
            mesh->stl.stats.type = inmemory;
            mesh->stl.stats.number_of_facets = (uint32_t)facets.size();
            mesh->stl.stats.original_num_facets = mesh->stl.stats.number_of_facets;
            stl_allocate(&mesh->stl);

            // Assign the facets to the new mesh.
            bool first = true;
            for (auto facet = facets.begin(); facet != facets.end(); ++ facet) {
                mesh->stl.facet_start[facet - facets.begin()] = this->stl.facet_start[*facet];
                stl_facet_stats(&mesh->stl, this->stl.facet_start[*facet], first);
            }
        }
    }

    return meshes;
}

void TriangleMesh::merge(const TriangleMesh &mesh)
{
    // reset stats and metadata
    int number_of_facets = this->stl.stats.number_of_facets;
    this->its.clear();
    this->repaired = false;
    
    // update facet count and allocate more memory
    this->stl.stats.number_of_facets = number_of_facets + mesh.stl.stats.number_of_facets;
    this->stl.stats.original_num_facets = this->stl.stats.number_of_facets;
    stl_reallocate(&this->stl);
    
    // copy facets
    for (uint32_t i = 0; i < mesh.stl.stats.number_of_facets; ++ i)
        this->stl.facet_start[number_of_facets + i] = mesh.stl.facet_start[i];
    
    // update size
    stl_get_size(&this->stl);
}

// Calculate projection of the mesh into the XY plane, in scaled coordinates.
//FIXME This could be extremely slow! Use it for tiny meshes only!
ExPolygons TriangleMesh::horizontal_projection() const
{
    ClipperLib::Paths   paths;
    Polygon             p;
    p.points.assign(3, Point());
    auto                delta = scaled<float>(0.01);
    std::vector<float>  deltas { delta, delta, delta };
    paths.reserve(this->stl.stats.number_of_facets);
    for (const stl_facet &facet : this->stl.facet_start) {
        p.points[0] = Point::new_scale(facet.vertex[0](0), facet.vertex[0](1));
        p.points[1] = Point::new_scale(facet.vertex[1](0), facet.vertex[1](1));
        p.points[2] = Point::new_scale(facet.vertex[2](0), facet.vertex[2](1));
        p.make_counter_clockwise();
        paths.emplace_back(mittered_offset_path_scaled(p.points, deltas, 3.));
    }
    
    // the offset factor was tuned using groovemount.stl
    return ClipperPaths_to_Slic3rExPolygons(paths);
}

// 2D convex hull of a 3D mesh projected into the Z=0 plane.
Polygon TriangleMesh::convex_hull()
{
    Points pp;
    pp.reserve(this->its.vertices.size());
    for (size_t i = 0; i < this->its.vertices.size(); ++ i) {
        const stl_vertex &v = this->its.vertices[i];
        pp.emplace_back(Point::new_scale(v(0), v(1)));
    }
    return Slic3r::Geometry::convex_hull(pp);
}

BoundingBoxf3 TriangleMesh::bounding_box() const
{
    BoundingBoxf3 bb;
    bb.defined = true;
    bb.min = this->stl.stats.min.cast<double>();
    bb.max = this->stl.stats.max.cast<double>();
    return bb;
}

BoundingBoxf3 TriangleMesh::transformed_bounding_box(const Transform3d &trafo) const
{
    BoundingBoxf3 bbox;
    if (this->its.vertices.empty()) {
        // Using the STL faces.
        for (const stl_facet &facet : this->stl.facet_start)
            for (size_t j = 0; j < 3; ++ j)
                bbox.merge(trafo * facet.vertex[j].cast<double>());
    } else {
        // Using the shared vertices should be a bit quicker than using the STL faces.
        for (const stl_vertex &v : this->its.vertices)
            bbox.merge(trafo * v.cast<double>());
    }
    return bbox;
}

TriangleMesh TriangleMesh::convex_hull_3d() const
{
    // The qhull call:
    orgQhull::Qhull qhull;
    qhull.disableOutputStream(); // we want qhull to be quiet
    std::vector<realT> src_vertices;
    try
    {
        if (this->has_shared_vertices()) {
#if REALfloat
            qhull.runQhull("", 3, (int)this->its.vertices.size(), (const realT*)(this->its.vertices.front().data()), "Qt");
#else
            src_vertices.reserve(this->its.vertices.size() * 3);
            // We will now fill the vector with input points for computation:
            for (const stl_vertex &v : this->its.vertices)
                for (int i = 0; i < 3; ++ i)
                    src_vertices.emplace_back(v(i));
            qhull.runQhull("", 3, (int)src_vertices.size() / 3, src_vertices.data(), "Qt");
#endif
        } else {
            src_vertices.reserve(this->stl.facet_start.size() * 9);
            // We will now fill the vector with input points for computation:
            for (const stl_facet &f : this->stl.facet_start)
                for (int i = 0; i < 3; ++ i)
                    for (int j = 0; j < 3; ++ j)
                        src_vertices.emplace_back(f.vertex[i](j));
            qhull.runQhull("", 3, (int)src_vertices.size() / 3, src_vertices.data(), "Qt");
        }
    }
    catch (...)
    {
        std::cout << "Unable to create convex hull" << std::endl;
        return TriangleMesh();
    }

    // Let's collect results:
    Pointf3s dst_vertices;
    std::vector<Vec3i> facets;
    auto facet_list = qhull.facetList().toStdVector();
    for (const orgQhull::QhullFacet& facet : facet_list)
    {   // iterate through facets
        orgQhull::QhullVertexSet vertices = facet.vertices();
        for (int i = 0; i < 3; ++i)
        {   // iterate through facet's vertices

            orgQhull::QhullPoint p = vertices[i].point();
            const auto* coords = p.coordinates();
            dst_vertices.emplace_back(coords[0], coords[1], coords[2]);
        }
        unsigned int size = (unsigned int)dst_vertices.size();
        facets.emplace_back(size - 3, size - 2, size - 1);
    }

    TriangleMesh output_mesh(dst_vertices, facets);
    output_mesh.repair();
    return output_mesh;
}

std::vector<ExPolygons> TriangleMesh::slice(const std::vector<double> &z) const
{
    // convert doubles to floats
    std::vector<float> z_f(z.begin(), z.end());
    assert(this->has_shared_vertices());
    return slice_mesh_ex(this->its, z_f, 0.0004f);
}

void TriangleMesh::require_shared_vertices()
{
    BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::require_shared_vertices - start";
    assert(stl_validate(&this->stl));
    if (! this->repaired) 
        this->repair();
    if (this->its.vertices.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::require_shared_vertices - stl_generate_shared_vertices";
        stl_generate_shared_vertices(&this->stl, this->its);
    }
    assert(stl_validate(&this->stl, this->its));
    BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::require_shared_vertices - end";
}

size_t TriangleMesh::memsize() const
{
    size_t memsize = 8 + this->stl.memsize() + this->its.memsize();
    return memsize;
}

// Release optional data from the mesh if the object is on the Undo / Redo stack only. Returns the amount of memory released.
size_t TriangleMesh::release_optional()
{
    size_t memsize_released = sizeof(stl_neighbors) * this->stl.neighbors_start.size() + this->its.memsize();
    // The indexed triangle set may be recalculated using the stl_generate_shared_vertices() function.
    this->its.clear();
    // The neighbors structure may be recalculated using the stl_check_facets_exact() function.
    this->stl.neighbors_start.clear();
    return memsize_released;
}

// Restore optional data possibly released by release_optional().
void TriangleMesh::restore_optional()
{
    if (! this->stl.facet_start.empty()) {
        // Save the old stats before calling stl_check_faces_exact, as it may modify the statistics.
        stl_stats stats = this->stl.stats;
        if (this->stl.neighbors_start.empty()) {
            stl_reallocate(&this->stl);
            stl_check_facets_exact(&this->stl);
        }
        if (this->its.vertices.empty())
            stl_generate_shared_vertices(&this->stl, this->its);
        // Restore the old statistics.
        this->stl.stats = stats;
    }
}

// Create a mapping from triangle edge into face.
struct EdgeToFace {
    // Index of the 1st vertex of the triangle edge. vertex_low <= vertex_high.
    int  vertex_low;
    // Index of the 2nd vertex of the triangle edge.
    int  vertex_high;
    // Index of a triangular face.
    int  face;
    // Index of edge in the face, starting with 1. Negative indices if the edge was stored reverse in (vertex_low, vertex_high).
    int  face_edge;
    bool operator==(const EdgeToFace &other) const { return vertex_low == other.vertex_low && vertex_high == other.vertex_high; }
    bool operator<(const EdgeToFace &other) const { return vertex_low < other.vertex_low || (vertex_low == other.vertex_low && vertex_high < other.vertex_high); }
};

template<typename ThrowOnCancelCallback>
static std::vector<EdgeToFace> create_edge_map(
    const indexed_triangle_set &its, ThrowOnCancelCallback throw_on_cancel)
{
    std::vector<EdgeToFace> edges_map;
    edges_map.assign(its.indices.size() * 3, EdgeToFace());
    for (uint32_t facet_idx = 0; facet_idx < its.indices.size(); ++ facet_idx)
        for (int i = 0; i < 3; ++ i) {
            EdgeToFace &e2f = edges_map[facet_idx * 3 + i];
            e2f.vertex_low  = its.indices[facet_idx][i];
            e2f.vertex_high = its.indices[facet_idx][(i + 1) % 3];
            e2f.face        = facet_idx;
            // 1 based indexing, to be always strictly positive.
            e2f.face_edge   = i + 1;
            if (e2f.vertex_low > e2f.vertex_high) {
                // Sort the vertices
                std::swap(e2f.vertex_low, e2f.vertex_high);
                // and make the face_edge negative to indicate a flipped edge.
                e2f.face_edge = - e2f.face_edge;
            }
        }
    throw_on_cancel();
    std::sort(edges_map.begin(), edges_map.end());

    return edges_map;
}

// Map from a face edge to a unique edge identifier or -1 if no neighbor exists.
// Two neighbor faces share a unique edge identifier even if they are flipped.
template<typename ThrowOnCancelCallback>
static inline std::vector<Vec3i> create_face_neighbors_index_impl(const indexed_triangle_set &its, ThrowOnCancelCallback throw_on_cancel)
{
    std::vector<Vec3i> out(its.indices.size(), Vec3i(-1, -1, -1));

    std::vector<EdgeToFace> edges_map = create_edge_map(its, throw_on_cancel);

    // Assign a unique common edge id to touching triangle edges.
    int num_edges = 0;
    for (size_t i = 0; i < edges_map.size(); ++ i) {
        EdgeToFace &edge_i = edges_map[i];
        if (edge_i.face == -1)
            // This edge has been connected to some neighbor already.
            continue;
        // Unconnected edge. Find its neighbor with the correct orientation.
        size_t j;
        bool found = false;
        for (j = i + 1; j < edges_map.size() && edge_i == edges_map[j]; ++ j)
            if (edge_i.face_edge * edges_map[j].face_edge < 0 && edges_map[j].face != -1) {
                // Faces touching with opposite oriented edges and none of the edges is connected yet.
                found = true;
                break;
            }
        if (! found) {
            //FIXME Vojtech: Trying to find an edge with equal orientation. This smells.
            // admesh can assign the same edge ID to more than two facets (which is 
            // still topologically correct), so we have to search for a duplicate of 
            // this edge too in case it was already seen in this orientation
            for (j = i + 1; j < edges_map.size() && edge_i == edges_map[j]; ++ j)
                if (edges_map[j].face != -1) {
                    // Faces touching with equally oriented edges and none of the edges is connected yet.
                    found = true;
                    break;
                }
        }
        // Assign an edge index to the 1st face.
        out[edge_i.face](std::abs(edge_i.face_edge) - 1) = num_edges;
        if (found) {
            EdgeToFace &edge_j = edges_map[j];
            out[edge_j.face](std::abs(edge_j.face_edge) - 1) = num_edges;
            // Mark the edge as connected.
            edge_j.face = -1;
        }
        ++ num_edges;
        if ((i & 0x0ffff) == 0)
            throw_on_cancel();
    }

    return out;
}

std::vector<Vec3i> create_face_neighbors_index(const indexed_triangle_set &its)
{
    return create_face_neighbors_index_impl(its, [](){});
}

std::vector<Vec3i> create_face_neighbors_index(const indexed_triangle_set &its, std::function<void()> throw_on_cancel_callback)
{
    return create_face_neighbors_index_impl(its, throw_on_cancel_callback);
}

// Merge duplicate vertices, return number of vertices removed.
int its_merge_vertices(indexed_triangle_set &its, bool shrink_to_fit)
{
    // 1) Sort indices to vertices lexicographically by coordinates AND vertex index.
    auto sorted = reserve_vector<int>(its.vertices.size());
    for (int i = 0; i < int(its.vertices.size()); ++ i)
        sorted.emplace_back(i);
    std::sort(sorted.begin(), sorted.end(), [&its](int il, int ir) {
        const Vec3f &l = its.vertices[il];
        const Vec3f &r = its.vertices[ir];
        // Sort lexicographically by coordinates AND vertex index.
        return l.x() < r.x() || (l.x() == r.x() && (l.y() < r.y() || (l.y() == r.y() && (l.z() < r.z() || (l.z() == r.z() && il < ir)))));
    });

    // 2) Map duplicate vertices to the one with the lowest vertex index.
    // The vertex to stay will have a map_vertices[...] == -1 index assigned, the other vertices will point to it.
    std::vector<int> map_vertices(its.vertices.size(), -1);
    for (int i = 0; i < int(sorted.size());) {
        const int    u = sorted[i];
        const Vec3f &p = its.vertices[u];
        int j = i;
        for (++ j; j < int(sorted.size()); ++ j) {
            const int    v = sorted[j];
            const Vec3f &q = its.vertices[v];
            if (p != q)
                break;
            assert(v > u);
            map_vertices[v] = u;
        }
        i = j;
    }

    // 3) Shrink its.vertices, update map_vertices with the new vertex indices.
    int k = 0;
    for (int i = 0; i < int(its.vertices.size()); ++ i) {
        if (map_vertices[i] == -1) {
            map_vertices[i] = k;
            if (k < i)
                its.vertices[k] = its.vertices[i];
            ++ k;
        } else {
            assert(map_vertices[i] < i);
            map_vertices[i] = map_vertices[map_vertices[i]];
        }
    }

    int num_erased = int(its.vertices.size()) - k;

    if (num_erased) {
        // Shrink the vertices.
        its.vertices.erase(its.vertices.begin() + k, its.vertices.end());
        // Remap face indices.
        for (stl_triangle_vertex_indices &face : its.indices)
            for (int i = 0; i < 3; ++ i)
                face(i) = map_vertices[face(i)];
        // Optionally shrink to fit (reallocate) vertices.
        if (shrink_to_fit)
            its.vertices.shrink_to_fit();
    }

    return num_erased;
}

void its_flip_triangles(indexed_triangle_set &its)
{
    for (stl_triangle_vertex_indices &face : its.indices)
        std::swap(face(1), face(2));
}

int its_remove_degenerate_faces(indexed_triangle_set &its, bool shrink_to_fit)
{
    int last = 0;
    for (int i = 0; i < int(its.indices.size()); ++ i) {
        const stl_triangle_vertex_indices &face = its.indices[i];
        if (face(0) != face(1) && face(0) != face(2) && face(1) != face(2)) {
            if (last < i)
                its.indices[last] = its.indices[i];
            ++ last;
        }
    }
    int removed = int(its.indices.size()) - last;
    if (removed) {
        its.indices.erase(its.indices.begin() + last, its.indices.end());
        // Optionally shrink the vertices.
        if (shrink_to_fit)
            its.indices.shrink_to_fit();
    }
    return removed;
}

int its_compactify_vertices(indexed_triangle_set &its, bool shrink_to_fit)
{
    // First used to mark referenced vertices, later used for mapping old vertex index to a new one.
    std::vector<int> vertex_map(its.vertices.size(), 0);
    // Mark referenced vertices.
    for (const stl_triangle_vertex_indices &face : its.indices)
        for (int i = 0; i < 3; ++ i)
            vertex_map[face(i)] = 1;
    // Compactify vertices, update map from old vertex index to a new one.
    int last = 0;
    for (int i = 0; i < int(vertex_map.size()); ++ i)
        if (vertex_map[i]) {
            if (last < i)
                its.vertices[last] = its.vertices[i];
            vertex_map[i] = last ++;
        }
    int removed = int(its.vertices.size()) - last;
    if (removed) {
        its.vertices.erase(its.vertices.begin() + last, its.vertices.end());
        // Update faces with the new vertex indices.
        for (stl_triangle_vertex_indices &face : its.indices)
            for (int i = 0; i < 3; ++ i)
                face(i) = vertex_map[face(i)];
        // Optionally shrink the vertices.
        if (shrink_to_fit)
            its.vertices.shrink_to_fit();
    }
    return removed;
}

void its_shrink_to_fit(indexed_triangle_set &its)
{
    its.indices.shrink_to_fit();
    its.vertices.shrink_to_fit();
}

template<typename TransformVertex>
void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const TransformVertex &transform_fn, const float z, Points &all_pts)
{
    all_pts.reserve(all_pts.size() + its.indices.size() * 3);
    for (const stl_triangle_vertex_indices &tri : its.indices) {
        const Vec3f pts[3] = { transform_fn(its.vertices[tri(0)]), transform_fn(its.vertices[tri(1)]), transform_fn(its.vertices[tri(2)]) };
        int iprev = 2;
        for (int iedge = 0; iedge < 3; ++ iedge) {
            const Vec3f &p1 = pts[iprev];
            const Vec3f &p2 = pts[iedge];
            if ((p1.z() < z && p2.z() > z) || (p2.z() < z && p1.z() > z)) {
                // Edge crosses the z plane. Calculate intersection point with the plane.
                float t = (z - p1.z()) / (p2.z() - p1.z());
                all_pts.emplace_back(scaled<coord_t>(p1.x() + (p2.x() - p1.x()) * t), scaled<coord_t>(p1.y() + (p2.y() - p1.y()) * t));
            }
            if (p2.z() >= z)
                all_pts.emplace_back(scaled<coord_t>(p2.x()), scaled<coord_t>(p2.y()));
            iprev = iedge;
        }
    }
}

void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const Matrix3f &m, const float z, Points &all_pts)
{
    return its_collect_mesh_projection_points_above(its, [m](const Vec3f &p){ return m * p; }, z, all_pts);
}

void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const Transform3f &t, const float z, Points &all_pts)
{
    return its_collect_mesh_projection_points_above(its, [t](const Vec3f &p){ return t * p; }, z, all_pts);
}

template<typename TransformVertex>
Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const TransformVertex &transform_fn, const float z)
{
    Points all_pts;
    its_collect_mesh_projection_points_above(its, transform_fn, z, all_pts);
    return Geometry::convex_hull(std::move(all_pts));
}

Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const Matrix3f &m, const float z)
{
    return its_convex_hull_2d_above(its, [m](const Vec3f &p){ return m * p; }, z);
}

Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const Transform3f &t, const float z)
{
    return its_convex_hull_2d_above(its, [t](const Vec3f &p){ return t * p; }, z);
}

// Generate the vertex list for a cube solid of arbitrary size in X/Y/Z.
indexed_triangle_set its_make_cube(double xd, double yd, double zd)
{
    auto x = float(xd), y = float(yd), z = float(zd);
    indexed_triangle_set mesh;
    mesh.vertices = {{x, y, 0}, {x, 0, 0}, {0, 0, 0}, {0, y, 0},
                     {x, y, z}, {0, y, z}, {0, 0, z}, {x, 0, z}};
    mesh.indices  = {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7},
                    {0, 4, 7}, {0, 7, 1}, {1, 7, 6}, {1, 6, 2},
                    {2, 6, 5}, {2, 5, 3}, {4, 0, 3}, {4, 3, 5}};

    return mesh;
}

TriangleMesh make_cube(double x, double y, double z) 
{
    TriangleMesh mesh(its_make_cube(x, y, z));
    mesh.repair();
    return mesh;
}

// Generate the mesh for a cylinder and return it, using 
// the generated angle to calculate the top mesh triangles.
// Default is 360 sides, angle fa is in radians.
indexed_triangle_set its_make_cylinder(double r, double h, double fa)
{
    indexed_triangle_set mesh;
    size_t n_steps    = (size_t)ceil(2. * PI / fa);
    double angle_step = 2. * PI / n_steps;

    auto &vertices = mesh.vertices;
    auto &facets   = mesh.indices;
    vertices.reserve(2 * n_steps + 2);
    facets.reserve(4 * n_steps);

    // 2 special vertices, top and bottom center, rest are relative to this
    vertices.emplace_back(Vec3f(0.f, 0.f, 0.f));
    vertices.emplace_back(Vec3f(0.f, 0.f, float(h)));

    // for each line along the polygon approximating the top/bottom of the
    // circle, generate four points and four facets (2 for the wall, 2 for the
    // top and bottom.
    // Special case: Last line shares 2 vertices with the first line.
    Vec2f p = Eigen::Rotation2Df(0.f) * Eigen::Vector2f(0, r);
    vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
    vertices.emplace_back(Vec3f(p(0), p(1), float(h)));
    for (size_t i = 1; i < n_steps; ++i) {
        p = Eigen::Rotation2Df(angle_step * i) * Eigen::Vector2f(0, float(r));
        vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
        vertices.emplace_back(Vec3f(p(0), p(1), float(h)));
        int id = (int)vertices.size() - 1;
        facets.emplace_back( 0, id - 1, id - 3); // top
        facets.emplace_back(id,      1, id - 2); // bottom
        facets.emplace_back(id, id - 2, id - 3); // upper-right of side
        facets.emplace_back(id, id - 3, id - 1); // bottom-left of side
    }
    // Connect the last set of vertices with the first.
    int id = (int)vertices.size() - 1;
    facets.emplace_back( 0, 2, id - 1);
    facets.emplace_back( 3, 1,     id);
    facets.emplace_back(id, 2,      3);
    facets.emplace_back(id, id - 1, 2);

    return mesh;
}

TriangleMesh make_cylinder(double r, double h, double fa)
{
    TriangleMesh mesh{its_make_cylinder(r, h, fa)};
    mesh.repair();

    return mesh;
}


TriangleMesh make_cone(double r, double h, double fa)
{
    Pointf3s vertices;
    std::vector<Vec3i>	facets;
    vertices.reserve(3+size_t(2*PI/fa));
    vertices.reserve(3+2*size_t(2*PI/fa));

    vertices = { Vec3d::Zero(), Vec3d(0., 0., h) }; // base center and top vertex
    size_t i = 0;
    for (double angle=0; angle<2*PI; angle+=fa) {
        vertices.emplace_back(r*std::cos(angle), r*std::sin(angle), 0.);
        if (angle > 0.) {
            facets.emplace_back(0, i+2, i+1);
            facets.emplace_back(1, i+1, i+2);
        }
        ++i;
    }
    facets.emplace_back(0, 2, i+1); // close the shape
    facets.emplace_back(1, i+1, 2);

    TriangleMesh mesh(std::move(vertices), std::move(facets));
    mesh.repair();
    return mesh;
}


// Generates mesh for a sphere centered about the origin, using the generated angle
// to determine the granularity. 
// Default angle is 1 degree.
//FIXME better to discretize an Icosahedron recursively http://www.songho.ca/opengl/gl_sphere.html
indexed_triangle_set its_make_sphere(double radius, double fa)
{
    int   sectorCount = int(ceil(2. * M_PI / fa));
    int   stackCount  = int(ceil(M_PI / fa));
    float sectorStep  = float(2. * M_PI / sectorCount);
    float stackStep   = float(M_PI / stackCount);

    indexed_triangle_set mesh;
    auto& vertices = mesh.vertices;
    vertices.reserve((stackCount - 1) * sectorCount + 2);
    for (int i = 0; i <= stackCount; ++ i) {
        // from pi/2 to -pi/2
        double stackAngle = 0.5 * M_PI - stackStep * i;
        double xy = radius * cos(stackAngle);
        double z  = radius * sin(stackAngle);
        if (i == 0 || i == stackCount)
            vertices.emplace_back(Vec3f(float(xy), 0.f, float(z)));
        else
            for (int j = 0; j < sectorCount; ++ j) {
                // from 0 to 2pi
                double sectorAngle = sectorStep * j;
                vertices.emplace_back(Vec3d(xy * std::cos(sectorAngle), xy * std::sin(sectorAngle), z).cast<float>());
            }
    }

    auto& facets = mesh.indices;
    facets.reserve(2 * (stackCount - 1) * sectorCount);
    for (int i = 0; i < stackCount; ++ i) {
        // Beginning of current stack.
        int k1 = (i == 0) ? 0 : (1 + (i - 1) * sectorCount);
        int k1_first = k1;
        // Beginning of next stack.
        int k2 = (i == 0) ? 1 : (k1 + sectorCount);
        int k2_first = k2;
        for (int j = 0; j < sectorCount; ++ j) {
            // 2 triangles per sector excluding first and last stacks
            int k1_next = k1;
            int k2_next = k2;
            if (i != 0) {
                k1_next = (j + 1 == sectorCount) ? k1_first : (k1 + 1);
                facets.emplace_back(k1, k2, k1_next);
            }
            if (i + 1 != stackCount) {
                k2_next = (j + 1 == sectorCount) ? k2_first : (k2 + 1);
                facets.emplace_back(k1_next, k2, k2_next);
            }
            k1 = k1_next;
            k2 = k2_next;
        }
    }

    return mesh;
}

TriangleMesh make_sphere(double radius, double fa)
{
    TriangleMesh mesh(its_make_sphere(radius, fa));
    mesh.repair();

    return mesh;
}

void its_merge(indexed_triangle_set &A, const indexed_triangle_set &B)
{
    auto N   = int(A.vertices.size());
    auto N_f = A.indices.size();

    A.vertices.insert(A.vertices.end(), B.vertices.begin(), B.vertices.end());
    A.indices.insert(A.indices.end(), B.indices.begin(), B.indices.end());

    for(size_t n = N_f; n < A.indices.size(); n++)
        A.indices[n] += Vec3i{N, N, N};
}

void its_merge(indexed_triangle_set &A, const std::vector<Vec3f> &triangles)
{
    const size_t offs = A.vertices.size();
    A.vertices.insert(A.vertices.end(), triangles.begin(), triangles.end());
    A.indices.reserve(A.indices.size() + A.vertices.size() / 3);

    for(int i = int(offs); i < int(A.vertices.size()); i += 3)
        A.indices.emplace_back(i, i + 1, i + 2);
}

void its_merge(indexed_triangle_set &A, const Pointf3s &triangles)
{
    auto trianglesf = reserve_vector<Vec3f> (triangles.size());
    for (auto &t : triangles)
        trianglesf.emplace_back(t.cast<float>());

    its_merge(A, trianglesf);
}

float its_volume(const indexed_triangle_set &its)
{
    if (its.empty()) return 0.;

    // Choose a point, any point as the reference.
    auto p0 = its.vertices.front();
    float volume = 0.f;
    for (size_t i = 0; i < its.indices.size(); ++ i) {
        // Do dot product to get distance from point to plane.
        its_triangle triangle = its_triangle_vertices(its, i);
        Vec3f U = triangle[1] - triangle[0];
        Vec3f V = triangle[2] - triangle[0];
        Vec3f C = U.cross(V);
        Vec3f normal = C.normalized();
        float area = 0.5 * C.norm();
        float height = normal.dot(triangle[0] - p0);
        volume += (area * height) / 3.0f;
    }

    return volume;
}

std::vector<indexed_triangle_set> its_split(const indexed_triangle_set &its)
{
    return its_split<>(its);
}

bool its_is_splittable(const indexed_triangle_set &its)
{
    return its_is_splittable<>(its);
}

void VertexFaceIndex::create(const indexed_triangle_set &its)
{
    m_vertex_to_face_start.assign(its.vertices.size() + 1, 0);
    // 1) Calculate vertex incidence by scatter.
    for (auto &face : its.indices) {
        ++ m_vertex_to_face_start[face(0) + 1];
        ++ m_vertex_to_face_start[face(1) + 1];
        ++ m_vertex_to_face_start[face(2) + 1];
    }
    // 2) Prefix sum to calculate offsets to m_vertex_faces_all.
    for (size_t i = 2; i < m_vertex_to_face_start.size(); ++ i)
        m_vertex_to_face_start[i] += m_vertex_to_face_start[i - 1];
    // 3) Scatter indices of faces incident to a vertex into m_vertex_faces_all.
    m_vertex_faces_all.assign(m_vertex_to_face_start.back(), 0);
    for (size_t face_idx = 0; face_idx < its.indices.size(); ++ face_idx) {
        auto &face = its.indices[face_idx];
        for (int i = 0; i < 3; ++ i)
            m_vertex_faces_all[m_vertex_to_face_start[face(i)] ++] = face_idx;
    }
    // 4) The previous loop modified m_vertex_to_face_start. Revert the change.
    for (auto i = int(m_vertex_to_face_start.size()) - 1; i > 0; -- i)
        m_vertex_to_face_start[i] = m_vertex_to_face_start[i - 1];
    m_vertex_to_face_start.front() = 0;
}

std::vector<Vec3i> its_create_neighbors_index(const indexed_triangle_set &its)
{
    return create_neighbors_index(ex_seq, its);
}

std::vector<Vec3i> its_create_neighbors_index_par(const indexed_triangle_set &its)
{
    return create_neighbors_index(ex_tbb, its);
}

} // namespace Slic3r
