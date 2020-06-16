#include "TriangleMesh.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "Tesselate.hpp"
#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullVertexSet.h>
#include <cmath>
#include <deque>
#include <queue>
#include <set>
#include <vector>
#include <map>
#include <utility>
#include <algorithm>
#include <math.h>
#include <type_traits>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>

#include <Eigen/Core>
#include <Eigen/Dense>

// for SLIC3R_DEBUG_SLICE_PROCESSING
#include "libslic3r.h"

#if 0
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
    #define SLIC3R_DEBUG
// #define SLIC3R_TRIANGLEMESH_DEBUG
#endif

#include <assert.h>

#if defined(SLIC3R_DEBUG) || defined(SLIC3R_DEBUG_SLICE_PROCESSING)
#include "SVG.hpp"
#endif

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

TriangleMesh::TriangleMesh(const indexed_triangle_set &M)
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
        // Left handed transformation is being applied. It is a good idea to flip the faces and their normals.
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
        throw std::runtime_error("find_unvisited_neighbors() requires repair()");

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
    // Loop while we have remaining facets.
    std::vector<unsigned char> facet_visited;
    TriangleMeshPtrs meshes;
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
    Polygons pp;
    pp.reserve(this->stl.stats.number_of_facets);
	for (const stl_facet &facet : this->stl.facet_start) {
        Polygon p;
        p.points.resize(3);
        p.points[0] = Point::new_scale(facet.vertex[0](0), facet.vertex[0](1));
        p.points[1] = Point::new_scale(facet.vertex[1](0), facet.vertex[1](1));
        p.points[2] = Point::new_scale(facet.vertex[2](0), facet.vertex[2](1));
        p.make_counter_clockwise();  // do this after scaling, as winding order might change while doing that
        pp.emplace_back(p);
    }
    
    // the offset factor was tuned using groovemount.stl
    return union_ex(offset(pp, scale_(0.01)), true);
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

std::vector<ExPolygons> TriangleMesh::slice(const std::vector<double> &z)
{
    // convert doubles to floats
    std::vector<float> z_f(z.begin(), z.end());
    TriangleMeshSlicer mslicer(this);
    std::vector<ExPolygons> layers;
    mslicer.slice(z_f, SlicingMode::Regular, 0.0004f, &layers, [](){});
    return layers;
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

void TriangleMeshSlicer::init(const TriangleMesh *_mesh, throw_on_cancel_callback_type throw_on_cancel)
{
    mesh = _mesh;
    if (! mesh->has_shared_vertices())
        throw std::invalid_argument("TriangleMeshSlicer was passed a mesh without shared vertices.");

    throw_on_cancel();
    facets_edges.assign(_mesh->stl.stats.number_of_facets * 3, -1);
	v_scaled_shared.assign(_mesh->its.vertices.size(), stl_vertex());
	for (size_t i = 0; i < v_scaled_shared.size(); ++ i)
        this->v_scaled_shared[i] = _mesh->its.vertices[i] / float(SCALING_FACTOR);

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
    std::vector<EdgeToFace> edges_map;
    edges_map.assign(this->mesh->stl.stats.number_of_facets * 3, EdgeToFace());
    for (uint32_t facet_idx = 0; facet_idx < this->mesh->stl.stats.number_of_facets; ++ facet_idx)
        for (int i = 0; i < 3; ++ i) {
            EdgeToFace &e2f = edges_map[facet_idx*3+i];
            e2f.vertex_low  = this->mesh->its.indices[facet_idx][i];
            e2f.vertex_high = this->mesh->its.indices[facet_idx][(i + 1) % 3];
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
        this->facets_edges[edge_i.face * 3 + std::abs(edge_i.face_edge) - 1] = num_edges;
        if (found) {
            EdgeToFace &edge_j = edges_map[j];
            this->facets_edges[edge_j.face * 3 + std::abs(edge_j.face_edge) - 1] = num_edges;
            // Mark the edge as connected.
            edge_j.face = -1;
        }
        ++ num_edges;
        if ((i & 0x0ffff) == 0)
            throw_on_cancel();
    }
}



void TriangleMeshSlicer::set_up_direction(const Vec3f& up)
{
    m_quaternion.setFromTwoVectors(up, Vec3f::UnitZ());
    m_use_quaternion = true;
}



void TriangleMeshSlicer::slice(const std::vector<float> &z, SlicingMode mode, std::vector<Polygons>* layers, throw_on_cancel_callback_type throw_on_cancel) const
{
    BOOST_LOG_TRIVIAL(debug) << "TriangleMeshSlicer::slice";

    /*
       This method gets called with a list of unscaled Z coordinates and outputs
       a vector pointer having the same number of items as the original list.
       Each item is a vector of polygons created by slicing our mesh at the 
       given heights.
       
       This method should basically combine the behavior of the existing
       Perl methods defined in lib/Slic3r/TriangleMesh.pm:
       
       - analyze(): this creates the 'facets_edges' and the 'edges_facets'
            tables (we don't need the 'edges' table)
       
       - slice_facet(): this has to be done for each facet. It generates 
            intersection lines with each plane identified by the Z list.
            The get_layer_range() binary search used to identify the Z range
            of the facet is already ported to C++ (see Object.xsp)
       
       - make_loops(): this has to be done for each layer. It creates polygons
            from the lines generated by the previous step.
        
        At the end, we free the tables generated by analyze() as we don't 
        need them anymore.
        
        NOTE: this method accepts a vector of floats because the mesh coordinate
        type is float.
    */
    
    BOOST_LOG_TRIVIAL(debug) << "TriangleMeshSlicer::_slice_do";
    std::vector<IntersectionLines> lines(z.size());
    {
        boost::mutex lines_mutex;
        tbb::parallel_for(
            tbb::blocked_range<int>(0,this->mesh->stl.stats.number_of_facets),
            [&lines, &lines_mutex, &z, throw_on_cancel, this](const tbb::blocked_range<int>& range) {
                for (int facet_idx = range.begin(); facet_idx < range.end(); ++ facet_idx) {
                    if ((facet_idx & 0x0ffff) == 0)
                        throw_on_cancel();
                    this->_slice_do(facet_idx, &lines, &lines_mutex, z);
                }
            }
        );
    }
    throw_on_cancel();

    // v_scaled_shared could be freed here
    
    // build loops
    BOOST_LOG_TRIVIAL(debug) << "TriangleMeshSlicer::_make_loops_do";
    layers->resize(z.size());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, z.size()),
        [&lines, &layers, mode, throw_on_cancel, this](const tbb::blocked_range<size_t>& range) {
            for (size_t line_idx = range.begin(); line_idx < range.end(); ++ line_idx) {
                if ((line_idx & 0x0ffff) == 0)
                    throw_on_cancel();

                Polygons &polygons = (*layers)[line_idx];
                this->make_loops(lines[line_idx], &polygons);

                if (! polygons.empty()) {
                    if (mode == SlicingMode::Positive) {
                        // Reorient all loops to be CCW.
                        for (Polygon& p : polygons)
                            p.make_counter_clockwise();
                    } else if (mode == SlicingMode::PositiveLargestContour) {
                        // Keep just the largest polygon, make it CCW.
                        double   max_area = 0.;
                        Polygon* max_area_polygon = nullptr;
                        for (Polygon& p : polygons) {
                            double a = p.area();
                            if (std::abs(a) > std::abs(max_area)) {
                                max_area = a;
                                max_area_polygon = &p;
                            }
                        }
                        assert(max_area_polygon != nullptr);
                        if (max_area < 0.)
                            max_area_polygon->reverse();
                        Polygon p(std::move(*max_area_polygon));
                        polygons.clear();
                        polygons.emplace_back(std::move(p));
                    }
                }
            }
        }
    );
    BOOST_LOG_TRIVIAL(debug) << "TriangleMeshSlicer::slice finished";

#ifdef SLIC3R_DEBUG
    {
        static int iRun = 0;
        for (size_t i = 0; i < z.size(); ++ i) {
            Polygons  &polygons   = (*layers)[i];
            ExPolygons expolygons = union_ex(polygons, true);
            SVG::export_expolygons(debug_out_path("slice_%d_%d.svg", iRun, i).c_str(), expolygons);
            {
                BoundingBox bbox;
                for (const IntersectionLine &l : lines[i]) {
                    bbox.merge(l.a);
                    bbox.merge(l.b);
                }
                SVG svg(debug_out_path("slice_loops_%d_%d.svg", iRun, i).c_str(), bbox);
                svg.draw(expolygons);
                for (const IntersectionLine &l : lines[i])
                    svg.draw(l, "red", 0);
                svg.draw_outline(expolygons, "black", "blue", 0);
                svg.Close();
            }
#if 0
//FIXME slice_facet() may create zero length edges due to rounding of doubles into coord_t.
            for (Polygon &poly : polygons) {
                for (size_t i = 1; i < poly.points.size(); ++ i)
                    assert(poly.points[i-1] != poly.points[i]);
                assert(poly.points.front() != poly.points.back());
            }
#endif
        }
        ++ iRun;
    }
#endif
}

void TriangleMeshSlicer::_slice_do(size_t facet_idx, std::vector<IntersectionLines>* lines, boost::mutex* lines_mutex, 
    const std::vector<float> &z) const
{
    const stl_facet &facet = m_use_quaternion ? (this->mesh->stl.facet_start.data() + facet_idx)->rotated(m_quaternion) : *(this->mesh->stl.facet_start.data() + facet_idx);
    
    // find facet extents
    const float min_z = fminf(facet.vertex[0](2), fminf(facet.vertex[1](2), facet.vertex[2](2)));
    const float max_z = fmaxf(facet.vertex[0](2), fmaxf(facet.vertex[1](2), facet.vertex[2](2)));
    
    #ifdef SLIC3R_TRIANGLEMESH_DEBUG
    printf("\n==> FACET %d (%f,%f,%f - %f,%f,%f - %f,%f,%f):\n", facet_idx,
        facet.vertex[0](0), facet.vertex[0](1), facet.vertex[0](2),
        facet.vertex[1](0), facet.vertex[1](1), facet.vertex[1](2),
        facet.vertex[2](0), facet.vertex[2](1), facet.vertex[2](2));
    printf("z: min = %.2f, max = %.2f\n", min_z, max_z);
    #endif /* SLIC3R_TRIANGLEMESH_DEBUG */
    
    // find layer extents
    std::vector<float>::const_iterator min_layer, max_layer;
    min_layer = std::lower_bound(z.begin(), z.end(), min_z); // first layer whose slice_z is >= min_z
    max_layer = std::upper_bound(min_layer, z.end(), max_z); // first layer whose slice_z is > max_z
    #ifdef SLIC3R_TRIANGLEMESH_DEBUG
    printf("layers: min = %d, max = %d\n", (int)(min_layer - z.begin()), (int)(max_layer - z.begin()));
    #endif /* SLIC3R_TRIANGLEMESH_DEBUG */
    
    for (std::vector<float>::const_iterator it = min_layer; it != max_layer; ++ it) {
        std::vector<float>::size_type layer_idx = it - z.begin();
        IntersectionLine il;
        if (this->slice_facet(*it / SCALING_FACTOR, facet, facet_idx, min_z, max_z, &il) == TriangleMeshSlicer::Slicing) {
            boost::lock_guard<boost::mutex> l(*lines_mutex);
            if (il.edge_type == feHorizontal) {
                // Ignore horizontal triangles. Any valid horizontal triangle must have a vertical triangle connected, otherwise the part has zero volume.
            } else
                (*lines)[layer_idx].emplace_back(il);
        }
    }
}

void TriangleMeshSlicer::slice(const std::vector<float> &z, SlicingMode mode, const float closing_radius, std::vector<ExPolygons>* layers, throw_on_cancel_callback_type throw_on_cancel) const
{
    std::vector<Polygons> layers_p;
    this->slice(z, (mode == SlicingMode::PositiveLargestContour) ? SlicingMode::Positive : mode, &layers_p, throw_on_cancel);
    
	BOOST_LOG_TRIVIAL(debug) << "TriangleMeshSlicer::make_expolygons in parallel - start";
	layers->resize(z.size());
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, z.size()),
		[&layers_p, mode, closing_radius, layers, throw_on_cancel, this](const tbb::blocked_range<size_t>& range) {
    		for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
#ifdef SLIC3R_TRIANGLEMESH_DEBUG
                printf("Layer %zu (slice_z = %.2f):\n", layer_id, z[layer_id]);
#endif
                throw_on_cancel();
                ExPolygons &expolygons = (*layers)[layer_id];
    			this->make_expolygons(layers_p[layer_id], closing_radius, &expolygons);
    			if (mode == SlicingMode::PositiveLargestContour)
					keep_largest_contour_only(expolygons);
    		}
    	});
	BOOST_LOG_TRIVIAL(debug) << "TriangleMeshSlicer::make_expolygons in parallel - end";
}

// Return true, if the facet has been sliced and line_out has been filled.
TriangleMeshSlicer::FacetSliceType TriangleMeshSlicer::slice_facet(
    float slice_z, const stl_facet &facet, const int facet_idx,
    const float min_z, const float max_z, 
    IntersectionLine *line_out) const
{
    IntersectionPoint points[3];
    size_t            num_points = 0;
    size_t            point_on_layer = size_t(-1);

    // Reorder vertices so that the first one is the one with lowest Z.
    // This is needed to get all intersection lines in a consistent order
    // (external on the right of the line)
    const stl_triangle_vertex_indices &vertices = this->mesh->its.indices[facet_idx];
    int i = (facet.vertex[1].z() == min_z) ? 1 : ((facet.vertex[2].z() == min_z) ? 2 : 0);

    // These are used only if the cut plane is tilted:
    stl_vertex rotated_a;
    stl_vertex rotated_b;

    for (int j = i; j - i < 3; ++j) {  // loop through facet edges
        int        edge_id  = this->facets_edges[facet_idx * 3 + (j % 3)];
        int        a_id     = vertices[j % 3];
        int        b_id     = vertices[(j+1) % 3];

        const stl_vertex *a;
        const stl_vertex *b;
        if (m_use_quaternion) {
            rotated_a = m_quaternion * this->v_scaled_shared[a_id];
            rotated_b = m_quaternion * this->v_scaled_shared[b_id];
            a = &rotated_a;
            b = &rotated_b;
        }
        else {
            a = &this->v_scaled_shared[a_id];
            b = &this->v_scaled_shared[b_id];
        }
        
        // Is edge or face aligned with the cutting plane?
        if (a->z() == slice_z && b->z() == slice_z) {
            // Edge is horizontal and belongs to the current layer.
            // The following rotation of the three vertices may not be efficient, but this branch happens rarely.
            const stl_vertex &v0 = m_use_quaternion ? stl_vertex(m_quaternion * this->v_scaled_shared[vertices[0]]) : this->v_scaled_shared[vertices[0]];
            const stl_vertex &v1 = m_use_quaternion ? stl_vertex(m_quaternion * this->v_scaled_shared[vertices[1]]) : this->v_scaled_shared[vertices[1]];
            const stl_vertex &v2 = m_use_quaternion ? stl_vertex(m_quaternion * this->v_scaled_shared[vertices[2]]) : this->v_scaled_shared[vertices[2]];
            const stl_normal &normal = facet.normal;
            // We may ignore this edge for slicing purposes, but we may still use it for object cutting.
            FacetSliceType    result = Slicing;
            if (min_z == max_z) {
                // All three vertices are aligned with slice_z.
                line_out->edge_type = feHorizontal;
                result = Cutting;
                if (normal.z() < 0) {
                    // If normal points downwards this is a bottom horizontal facet so we reverse its point order.
                    std::swap(a, b);
                    std::swap(a_id, b_id);
                }
            } else {
                // Two vertices are aligned with the cutting plane, the third vertex is below or above the cutting plane.
                // Is the third vertex below the cutting plane?
                bool third_below = v0.z() < slice_z || v1.z() < slice_z || v2.z() < slice_z;
                // Two vertices on the cutting plane, the third vertex is below the plane. Consider the edge to be part of the slice
                // only if it is the upper edge.
                // (the bottom most edge resp. vertex of a triangle is not owned by the triangle, but the top most edge resp. vertex is part of the triangle
                // in respect to the cutting plane).
                result = third_below ? Slicing : Cutting;
                if (third_below) {
                    line_out->edge_type = feTop;
                    std::swap(a, b);
                    std::swap(a_id, b_id);
                } else
                    line_out->edge_type = feBottom;
            }
            line_out->a.x()  = a->x();
            line_out->a.y()  = a->y();
            line_out->b.x()  = b->x();
            line_out->b.y()  = b->y();
            line_out->a_id   = a_id;
            line_out->b_id   = b_id;
            assert(line_out->a != line_out->b);
            return result;
        }

        if (a->z() == slice_z) {
            // Only point a alings with the cutting plane.
            if (point_on_layer == size_t(-1) || points[point_on_layer].point_id != a_id) {
                point_on_layer = num_points;
                IntersectionPoint &point = points[num_points ++];
                point.x()      = a->x();
                point.y()      = a->y();
                point.point_id = a_id;
            }
        } else if (b->z() == slice_z) {
            // Only point b alings with the cutting plane.
            if (point_on_layer == size_t(-1) || points[point_on_layer].point_id != b_id) {
                point_on_layer = num_points;
                IntersectionPoint &point = points[num_points ++];
                point.x()      = b->x();
                point.y()      = b->y();
                point.point_id = b_id;
            }
        } else if ((a->z() < slice_z && b->z() > slice_z) || (b->z() < slice_z && a->z() > slice_z)) {
            // A general case. The face edge intersects the cutting plane. Calculate the intersection point.
            assert(a_id != b_id);
            // Sort the edge to give a consistent answer.
            if (a_id > b_id) {
                std::swap(a_id, b_id);
                std::swap(a, b);
            }
            IntersectionPoint &point = points[num_points];
			double t = (double(slice_z) - double(b->z())) / (double(a->z()) - double(b->z()));
            if (t <= 0.) {
                if (point_on_layer == size_t(-1) || points[point_on_layer].point_id != a_id) {
                    point.x() = a->x();
                    point.y() = a->y();
                    point_on_layer = num_points ++;
                    point.point_id = a_id;
                }
            } else if (t >= 1.) {
                if (point_on_layer == size_t(-1) || points[point_on_layer].point_id != b_id) {
                    point.x() = b->x();
                    point.y() = b->y();
                    point_on_layer = num_points ++;
                    point.point_id = b_id;
                }
            } else {
                point.x() = coord_t(floor(double(b->x()) + (double(a->x()) - double(b->x())) * t + 0.5));
                point.y() = coord_t(floor(double(b->y()) + (double(a->y()) - double(b->y())) * t + 0.5));
                point.edge_id = edge_id;
                ++ num_points;
            }
        }
    }

    // Facets must intersect each plane 0 or 2 times, or it may touch the plane at a single vertex only.
    assert(num_points < 3);
    if (num_points == 2) {
        line_out->edge_type  = feGeneral;
        line_out->a          = (Point)points[1];
        line_out->b          = (Point)points[0];
        line_out->a_id       = points[1].point_id;
        line_out->b_id       = points[0].point_id;
        line_out->edge_a_id  = points[1].edge_id;
        line_out->edge_b_id  = points[0].edge_id;
        // Not a zero lenght edge.
        //FIXME slice_facet() may create zero length edges due to rounding of doubles into coord_t.
        //assert(line_out->a != line_out->b);
        // The plane cuts at least one edge in a general position.
        assert(line_out->a_id == -1 || line_out->b_id == -1);
        assert(line_out->edge_a_id != -1 || line_out->edge_b_id != -1);
        // General slicing position, use the segment for both slicing and object cutting.
#if 0
        if (line_out->a_id != -1 && line_out->b_id != -1) {
            // Solving a degenerate case, where both the intersections snapped to an edge.
            // Correctly classify the face as below or above based on the position of the 3rd point.
            int i = vertices[0];
            if (i == line_out->a_id || i == line_out->b_id)
                i = vertices[1];
            if (i == line_out->a_id || i == line_out->b_id)
                i = vertices[2];
            assert(i != line_out->a_id && i != line_out->b_id);
            line_out->edge_type = ((m_use_quaternion ?
                                    (m_quaternion * this->v_scaled_shared[i]).z()
                                    : this->v_scaled_shared[i].z()) < slice_z) ? feTop : feBottom;
        }
#endif
        return Slicing;
    }
    return NoSlice;
}

//FIXME Should this go away? For valid meshes the function slice_facet() returns Slicing
// and sets edges of vertical triangles to produce only a single edge per pair of neighbor faces.
// So the following code makes only sense now to handle degenerate meshes with more than two faces
// sharing a single edge.
static inline void remove_tangent_edges(std::vector<IntersectionLine> &lines)
{
    std::vector<IntersectionLine*> by_vertex_pair;
    by_vertex_pair.reserve(lines.size());
    for (IntersectionLine& line : lines)
        if (line.edge_type != feGeneral && line.a_id != -1)
            // This is a face edge. Check whether there is its neighbor stored in lines.
            by_vertex_pair.emplace_back(&line);
    auto edges_lower_sorted = [](const IntersectionLine *l1, const IntersectionLine *l2) {
        // Sort vertices of l1, l2 lexicographically
        int l1a = l1->a_id;
        int l1b = l1->b_id;
        int l2a = l2->a_id;
        int l2b = l2->b_id;
        if (l1a > l1b)
            std::swap(l1a, l1b);
        if (l2a > l2b)
            std::swap(l2a, l2b);
        // Lexicographical "lower" operator on lexicographically sorted vertices should bring equal edges together when sored.
        return l1a < l2a || (l1a == l2a && l1b < l2b);
    };
    std::sort(by_vertex_pair.begin(), by_vertex_pair.end(), edges_lower_sorted);
    for (auto line = by_vertex_pair.begin(); line != by_vertex_pair.end(); ++ line) {
        IntersectionLine &l1 = **line;
        if (! l1.skip()) {
            // Iterate as long as line and line2 edges share the same end points.
            for (auto line2 = line + 1; line2 != by_vertex_pair.end() && ! edges_lower_sorted(*line, *line2); ++ line2) {
                // Lines must share the end points.
                assert(! edges_lower_sorted(*line, *line2));
                assert(! edges_lower_sorted(*line2, *line));
                IntersectionLine &l2 = **line2;
                if (l2.skip())
                    continue;
                if (l1.a_id == l2.a_id) {
                    assert(l1.b_id == l2.b_id);
                    l2.set_skip();
                    // If they are both oriented upwards or downwards (like a 'V'),
                    // then we can remove both edges from this layer since it won't 
                    // affect the sliced shape.
                    // If one of them is oriented upwards and the other is oriented
                    // downwards, let's only keep one of them (it doesn't matter which
                    // one since all 'top' lines were reversed at slicing).
                    if (l1.edge_type == l2.edge_type) {
                        l1.set_skip();
                        break;
                    }
                } else {
                    assert(l1.a_id == l2.b_id && l1.b_id == l2.a_id);
                    // If this edge joins two horizontal facets, remove both of them.
                    if (l1.edge_type == feHorizontal && l2.edge_type == feHorizontal) {
                        l1.set_skip();
                        l2.set_skip();
                        break;
                    }
                }
            }
        }
    }
}


struct OpenPolyline {
    OpenPolyline() {};
    OpenPolyline(const IntersectionReference &start, const IntersectionReference &end, Points &&points) : 
        start(start), end(end), points(std::move(points)), consumed(false) { this->length = Slic3r::length(this->points); }
    void reverse() {
        std::swap(start, end);
        std::reverse(points.begin(), points.end());
    }
    IntersectionReference   start;
    IntersectionReference   end;
    Points                  points;
    double                  length;
    bool                    consumed;
};

// called by TriangleMeshSlicer::make_loops() to connect sliced triangles into closed loops and open polylines by the triangle connectivity.
// Only connects segments crossing triangles of the same orientation.
static void chain_lines_by_triangle_connectivity(std::vector<IntersectionLine> &lines, Polygons &loops, std::vector<OpenPolyline> &open_polylines)
{
    // Build a map of lines by edge_a_id and a_id.
    std::vector<IntersectionLine*> by_edge_a_id;
    std::vector<IntersectionLine*> by_a_id;
    by_edge_a_id.reserve(lines.size());
    by_a_id.reserve(lines.size());
    for (IntersectionLine &line : lines) {
        if (! line.skip()) {
            if (line.edge_a_id != -1)
                by_edge_a_id.emplace_back(&line);
            if (line.a_id != -1)
                by_a_id.emplace_back(&line);
        }
    }
    auto by_edge_lower = [](const IntersectionLine* il1, const IntersectionLine *il2) { return il1->edge_a_id < il2->edge_a_id; };
    auto by_vertex_lower = [](const IntersectionLine* il1, const IntersectionLine *il2) { return il1->a_id < il2->a_id; };
    std::sort(by_edge_a_id.begin(), by_edge_a_id.end(), by_edge_lower);
    std::sort(by_a_id.begin(), by_a_id.end(), by_vertex_lower);
    // Chain the segments with a greedy algorithm, collect the loops and unclosed polylines.
    IntersectionLines::iterator it_line_seed = lines.begin();
    for (;;) {
        // take first spare line and start a new loop
        IntersectionLine *first_line = nullptr;
        for (; it_line_seed != lines.end(); ++ it_line_seed)
            if (it_line_seed->is_seed_candidate()) {
            //if (! it_line_seed->skip()) {
                first_line = &(*it_line_seed ++);
                break;
            }
        if (first_line == nullptr)
            break;
        first_line->set_skip();
        Points loop_pts;
        loop_pts.emplace_back(first_line->a);
        IntersectionLine *last_line = first_line;
        
        /*
        printf("first_line edge_a_id = %d, edge_b_id = %d, a_id = %d, b_id = %d, a = %d,%d, b = %d,%d\n", 
            first_line->edge_a_id, first_line->edge_b_id, first_line->a_id, first_line->b_id,
            first_line->a.x, first_line->a.y, first_line->b.x, first_line->b.y);
        */
        
        IntersectionLine key;
        for (;;) {
            // find a line starting where last one finishes
            IntersectionLine* next_line = nullptr;
            if (last_line->edge_b_id != -1) {
                key.edge_a_id = last_line->edge_b_id;
                auto it_begin = std::lower_bound(by_edge_a_id.begin(), by_edge_a_id.end(), &key, by_edge_lower);
                if (it_begin != by_edge_a_id.end()) {
                    auto it_end = std::upper_bound(it_begin, by_edge_a_id.end(), &key, by_edge_lower);
                    for (auto it_line = it_begin; it_line != it_end; ++ it_line)
                        if (! (*it_line)->skip()) {
                            next_line = *it_line;
                            break;
                        }
                }
            }
            if (next_line == nullptr && last_line->b_id != -1) {
                key.a_id = last_line->b_id;
                auto it_begin = std::lower_bound(by_a_id.begin(), by_a_id.end(), &key, by_vertex_lower);
                if (it_begin != by_a_id.end()) {
                    auto it_end = std::upper_bound(it_begin, by_a_id.end(), &key, by_vertex_lower);
                    for (auto it_line = it_begin; it_line != it_end; ++ it_line)
                        if (! (*it_line)->skip()) {
                            next_line = *it_line;
                            break;
                        }
                }
            }
            if (next_line == nullptr) {
                // Check whether we closed this loop.
                if ((first_line->edge_a_id != -1 && first_line->edge_a_id == last_line->edge_b_id) || 
                    (first_line->a_id      != -1 && first_line->a_id      == last_line->b_id)) {
                    // The current loop is complete. Add it to the output.
                    loops.emplace_back(std::move(loop_pts));
                    #ifdef SLIC3R_TRIANGLEMESH_DEBUG
                    printf("  Discovered %s polygon of %d points\n", (p.is_counter_clockwise() ? "ccw" : "cw"), (int)p.points.size());
                    #endif
                } else {
                    // This is an open polyline. Add it to the list of open polylines. These open polylines will processed later.
                    loop_pts.emplace_back(last_line->b);
                    open_polylines.emplace_back(OpenPolyline(
                        IntersectionReference(first_line->a_id, first_line->edge_a_id), 
                        IntersectionReference(last_line->b_id, last_line->edge_b_id), std::move(loop_pts)));
                }
                break;
            }
            /*
            printf("next_line edge_a_id = %d, edge_b_id = %d, a_id = %d, b_id = %d, a = %d,%d, b = %d,%d\n", 
                next_line->edge_a_id, next_line->edge_b_id, next_line->a_id, next_line->b_id,
                next_line->a.x, next_line->a.y, next_line->b.x, next_line->b.y);
            */
            loop_pts.emplace_back(next_line->a);
            last_line = next_line;
            next_line->set_skip();
        }
    }
}

std::vector<OpenPolyline*> open_polylines_sorted(std::vector<OpenPolyline> &open_polylines, bool update_lengths)
{
    std::vector<OpenPolyline*> out;
    out.reserve(open_polylines.size());
    for (OpenPolyline &opl : open_polylines)
        if (! opl.consumed) {
            if (update_lengths)
                opl.length = Slic3r::length(opl.points);
            out.emplace_back(&opl);
        }
    std::sort(out.begin(), out.end(), [](const OpenPolyline *lhs, const OpenPolyline *rhs){ return lhs->length > rhs->length; });
    return out;
}

// called by TriangleMeshSlicer::make_loops() to connect remaining open polylines across shared triangle edges and vertices.
// Depending on "try_connect_reversed", it may or may not connect segments crossing triangles of opposite orientation.
static void chain_open_polylines_exact(std::vector<OpenPolyline> &open_polylines, Polygons &loops, bool try_connect_reversed)
{
    // Store the end points of open_polylines into vectors sorted
    struct OpenPolylineEnd {
        OpenPolylineEnd(OpenPolyline *polyline, bool start) : polyline(polyline), start(start) {}
        OpenPolyline    *polyline;
        // Is it the start or end point?
        bool             start;
        const IntersectionReference& ipref() const { return start ? polyline->start : polyline->end; }
        // Return a unique ID for the intersection point.
        // Return a positive id for a point, or a negative id for an edge.
        int id() const { const IntersectionReference &r = ipref(); return (r.point_id >= 0) ? r.point_id : - r.edge_id; }
        bool operator==(const OpenPolylineEnd &rhs) const { return this->polyline == rhs.polyline && this->start == rhs.start; }
    };
    auto by_id_lower = [](const OpenPolylineEnd &ope1, const OpenPolylineEnd &ope2) { return ope1.id() < ope2.id(); };
    std::vector<OpenPolylineEnd> by_id;
    by_id.reserve(2 * open_polylines.size());
    for (OpenPolyline &opl : open_polylines) {
        if (opl.start.point_id != -1 || opl.start.edge_id != -1)
            by_id.emplace_back(OpenPolylineEnd(&opl, true));
        if (try_connect_reversed && (opl.end.point_id != -1 || opl.end.edge_id != -1))
            by_id.emplace_back(OpenPolylineEnd(&opl, false));
    }
    std::sort(by_id.begin(), by_id.end(), by_id_lower);
    // Find an iterator to by_id_lower for the particular end of OpenPolyline (by comparing the OpenPolyline pointer and the start attribute).
    auto find_polyline_end = [&by_id, by_id_lower](const OpenPolylineEnd &end) -> std::vector<OpenPolylineEnd>::iterator {
        for (auto it = std::lower_bound(by_id.begin(), by_id.end(), end, by_id_lower);
                  it != by_id.end() && it->id() == end.id(); ++ it)
            if (*it == end)
                return it;
        return by_id.end();
    };
    // Try to connect the loops.
    std::vector<OpenPolyline*> sorted_by_length = open_polylines_sorted(open_polylines, false);
    for (OpenPolyline *opl : sorted_by_length) {
        if (opl->consumed)
            continue;
        opl->consumed = true;
        OpenPolylineEnd end(opl, false);
        for (;;) {
            // find a line starting where last one finishes
            auto it_next_start = std::lower_bound(by_id.begin(), by_id.end(), end, by_id_lower);
            for (; it_next_start != by_id.end() && it_next_start->id() == end.id(); ++ it_next_start)
                if (! it_next_start->polyline->consumed)
                    goto found;
            // The current loop could not be closed. Unmark the segment.
            opl->consumed = false;
            break;
        found:
            // Attach this polyline to the end of the initial polyline.
            if (it_next_start->start) {
                auto it = it_next_start->polyline->points.begin();
                std::copy(++ it, it_next_start->polyline->points.end(), back_inserter(opl->points));
            } else {
                auto it = it_next_start->polyline->points.rbegin();
                std::copy(++ it, it_next_start->polyline->points.rend(), back_inserter(opl->points));
            }
            opl->length += it_next_start->polyline->length;
            // Mark the next polyline as consumed.
            it_next_start->polyline->points.clear();
            it_next_start->polyline->length = 0.;
            it_next_start->polyline->consumed = true;
            if (try_connect_reversed) {
                // Running in a mode, where the polylines may be connected by mixing their orientations.
                // Update the end point lookup structure after the end point of the current polyline was extended.
                auto it_end      = find_polyline_end(end);
                auto it_next_end = find_polyline_end(OpenPolylineEnd(it_next_start->polyline, !it_next_start->start));
                // Swap the end points of the current and next polyline, but keep the polyline ptr and the start flag.
                std::swap(opl->end, it_next_end->start ? it_next_end->polyline->start : it_next_end->polyline->end);
                // Swap the positions of OpenPolylineEnd structures in the sorted array to match their respective end point positions.
                std::swap(*it_end, *it_next_end);
            }
            // Check whether we closed this loop.
            if ((opl->start.edge_id  != -1 && opl->start.edge_id  == opl->end.edge_id) ||
                (opl->start.point_id != -1 && opl->start.point_id == opl->end.point_id)) {
                // The current loop is complete. Add it to the output.
                //assert(opl->points.front().point_id == opl->points.back().point_id);
                //assert(opl->points.front().edge_id  == opl->points.back().edge_id);
                // Remove the duplicate last point.
                opl->points.pop_back();
                if (opl->points.size() >= 3) {
                    if (try_connect_reversed && area(opl->points) < 0)
                        // The closed polygon is patched from pieces with messed up orientation, therefore
                        // the orientation of the patched up polygon is not known.
                        // Orient the patched up polygons CCW. This heuristic may close some holes and cavities.
                        std::reverse(opl->points.begin(), opl->points.end());
                    loops.emplace_back(std::move(opl->points));
                }
                opl->points.clear();
                break;
            }
            // Continue with the current loop.
        }
    }
}

// called by TriangleMeshSlicer::make_loops() to connect remaining open polylines across shared triangle edges and vertices, 
// possibly closing small gaps.
// Depending on "try_connect_reversed", it may or may not connect segments crossing triangles of opposite orientation.
static void chain_open_polylines_close_gaps(std::vector<OpenPolyline> &open_polylines, Polygons &loops, double max_gap, bool try_connect_reversed)
{
    const coord_t max_gap_scaled = (coord_t)scale_(max_gap);

    // Sort the open polylines by their length, so the new loops will be seeded from longer chains.
    // Update the polyline lengths, return only not yet consumed polylines.
    std::vector<OpenPolyline*> sorted_by_length = open_polylines_sorted(open_polylines, true);

    // Store the end points of open_polylines into ClosestPointInRadiusLookup<OpenPolylineEnd>.
    struct OpenPolylineEnd {
        OpenPolylineEnd(OpenPolyline *polyline, bool start) : polyline(polyline), start(start) {}
        OpenPolyline    *polyline;
        // Is it the start or end point?
        bool             start;
        const Point&     point() const { return start ? polyline->points.front() : polyline->points.back(); }
        bool operator==(const OpenPolylineEnd &rhs) const { return this->polyline == rhs.polyline && this->start == rhs.start; }
    };
    struct OpenPolylineEndAccessor {
        const Point* operator()(const OpenPolylineEnd &pt) const { return pt.polyline->consumed ? nullptr : &pt.point(); }
    };
    typedef ClosestPointInRadiusLookup<OpenPolylineEnd, OpenPolylineEndAccessor> ClosestPointLookupType;
    ClosestPointLookupType closest_end_point_lookup(max_gap_scaled);
    for (OpenPolyline *opl : sorted_by_length) {
        closest_end_point_lookup.insert(OpenPolylineEnd(opl, true));
        if (try_connect_reversed)
            closest_end_point_lookup.insert(OpenPolylineEnd(opl, false));
    }
    // Try to connect the loops.
    for (OpenPolyline *opl : sorted_by_length) {
        if (opl->consumed)
            continue;
        OpenPolylineEnd end(opl, false);
        if (try_connect_reversed)
            // The end point of this polyline will be modified, thus the following entry will become invalid. Remove it.
            closest_end_point_lookup.erase(end);
        opl->consumed = true;
        size_t n_segments_joined = 1;
        for (;;) {
            // Find a line starting where last one finishes, only return non-consumed open polylines (OpenPolylineEndAccessor returns null for consumed).
            std::pair<const OpenPolylineEnd*, double> next_start_and_dist = closest_end_point_lookup.find(end.point());
            const OpenPolylineEnd *next_start = next_start_and_dist.first;
            // Check whether we closed this loop.
            double current_loop_closing_distance2 = (opl->points.back() - opl->points.front()).cast<double>().squaredNorm();
            bool   loop_closed = current_loop_closing_distance2 < coordf_t(max_gap_scaled) * coordf_t(max_gap_scaled);
            if (next_start != nullptr && loop_closed && current_loop_closing_distance2 < next_start_and_dist.second) {
                // Heuristics to decide, whether to close the loop, or connect another polyline.
                // One should avoid closing loops shorter than max_gap_scaled.
                loop_closed = sqrt(current_loop_closing_distance2) < 0.3 * length(opl->points);
            }
            if (loop_closed) {
                // Remove the start point of the current polyline from the lookup.
                // Mark the current segment as not consumed, otherwise the closest_end_point_lookup.erase() would fail.
                opl->consumed = false;
                closest_end_point_lookup.erase(OpenPolylineEnd(opl, true));
                if (current_loop_closing_distance2 == 0.) {
                    // Remove the duplicate last point.
                    opl->points.pop_back();
                } else {
                    // The end points are different, keep both of them.
                }
                if (opl->points.size() >= 3) {
                    if (try_connect_reversed && n_segments_joined > 1 && area(opl->points) < 0)
                        // The closed polygon is patched from pieces with messed up orientation, therefore
                        // the orientation of the patched up polygon is not known.
                        // Orient the patched up polygons CCW. This heuristic may close some holes and cavities.
                        std::reverse(opl->points.begin(), opl->points.end());
                    loops.emplace_back(std::move(opl->points));
                }
                opl->points.clear();
                opl->consumed = true;
                break;
            }
            if (next_start == nullptr) {
                // The current loop could not be closed. Unmark the segment.
                opl->consumed = false;
                if (try_connect_reversed)
                    // Re-insert the end point.
                    closest_end_point_lookup.insert(OpenPolylineEnd(opl, false));
                break;
            }
            // Attach this polyline to the end of the initial polyline.
            if (next_start->start) {
                auto it = next_start->polyline->points.begin();
                if (*it == opl->points.back())
                    ++ it;
                std::copy(it, next_start->polyline->points.end(), back_inserter(opl->points));
            } else {
                auto it = next_start->polyline->points.rbegin();
                if (*it == opl->points.back())
                    ++ it;
                std::copy(it, next_start->polyline->points.rend(), back_inserter(opl->points));
            }
            ++ n_segments_joined;
            // Remove the end points of the consumed polyline segment from the lookup.
            OpenPolyline *opl2 = next_start->polyline;
            closest_end_point_lookup.erase(OpenPolylineEnd(opl2, true));
            if (try_connect_reversed)
                closest_end_point_lookup.erase(OpenPolylineEnd(opl2, false));
            opl2->points.clear();
            opl2->consumed = true;
            // Continue with the current loop.
        }
    }
}

void TriangleMeshSlicer::make_loops(std::vector<IntersectionLine> &lines, Polygons* loops) const
{
#if 0
//FIXME slice_facet() may create zero length edges due to rounding of doubles into coord_t.
//#ifdef _DEBUG
    for (const Line &l : lines)
        assert(l.a != l.b);
#endif /* _DEBUG */

    // There should be no tangent edges, as the horizontal triangles are ignored and if two triangles touch at a cutting plane,
    // only the bottom triangle is considered to be cutting the plane.
//    remove_tangent_edges(lines);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        BoundingBox bbox_svg;
        {
            static int iRun = 0;
            for (const Line &line : lines) {
                bbox_svg.merge(line.a);
                bbox_svg.merge(line.b);
            }
            SVG svg(debug_out_path("TriangleMeshSlicer_make_loops-raw_lines-%d.svg", iRun ++).c_str(), bbox_svg);
            for (const Line &line : lines)
                svg.draw(line);
            svg.Close();
        }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    std::vector<OpenPolyline> open_polylines;
    chain_lines_by_triangle_connectivity(lines, *loops, open_polylines);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        {
            static int iRun = 0;
            SVG svg(debug_out_path("TriangleMeshSlicer_make_loops-polylines-%d.svg", iRun ++).c_str(), bbox_svg);
            svg.draw(union_ex(*loops));
            for (const OpenPolyline &pl : open_polylines)
                svg.draw(Polyline(pl.points), "red");
            svg.Close();
        }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Now process the open polylines.
    // Do it in two rounds, first try to connect in the same direction only,
    // then try to connect the open polylines in reversed order as well.
    chain_open_polylines_exact(open_polylines, *loops, false);
    chain_open_polylines_exact(open_polylines, *loops, true);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    {
        static int iRun = 0;
        SVG svg(debug_out_path("TriangleMeshSlicer_make_loops-polylines2-%d.svg", iRun++).c_str(), bbox_svg);
        svg.draw(union_ex(*loops));
        for (const OpenPolyline &pl : open_polylines) {
            if (pl.points.empty())
                continue;
            svg.draw(Polyline(pl.points), "red");
            svg.draw(pl.points.front(), "blue");
            svg.draw(pl.points.back(), "blue");
        }
        svg.Close();
    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Try to close gaps.
    // Do it in two rounds, first try to connect in the same direction only,
    // then try to connect the open polylines in reversed order as well.
#if 0
    for (double max_gap : { EPSILON, 0.001, 0.1, 1., 2. }) {
        chain_open_polylines_close_gaps(open_polylines, *loops, max_gap, false);
        chain_open_polylines_close_gaps(open_polylines, *loops, max_gap, true);
    }
#else
    const double max_gap = 2.; //mm
    chain_open_polylines_close_gaps(open_polylines, *loops, max_gap, false);
    chain_open_polylines_close_gaps(open_polylines, *loops, max_gap, true);
#endif

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    {
        static int iRun = 0;
        SVG svg(debug_out_path("TriangleMeshSlicer_make_loops-polylines-final-%d.svg", iRun++).c_str(), bbox_svg);
        svg.draw(union_ex(*loops));
        for (const OpenPolyline &pl : open_polylines) {
            if (pl.points.empty())
                continue;
            svg.draw(Polyline(pl.points), "red");
            svg.draw(pl.points.front(), "blue");
            svg.draw(pl.points.back(), "blue");
        }
        svg.Close();
    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

// Only used to cut the mesh into two halves.
void TriangleMeshSlicer::make_expolygons_simple(std::vector<IntersectionLine> &lines, ExPolygons* slices) const
{
    assert(slices->empty());

    Polygons loops;
    this->make_loops(lines, &loops);
    
    Polygons holes;
    for (Polygons::const_iterator loop = loops.begin(); loop != loops.end(); ++ loop) {
        if (loop->area() >= 0.) {
            ExPolygon ex;
            ex.contour = *loop;
            slices->emplace_back(ex);
        } else {
            holes.emplace_back(*loop);
        }
    }

    // If there are holes, then there should also be outer contours.
    assert(holes.empty() || ! slices->empty());
    if (slices->empty())
        return;
    
    // Assign holes to outer contours.
    for (Polygons::const_iterator hole = holes.begin(); hole != holes.end(); ++ hole) {
        // Find an outer contour to a hole.
        int     slice_idx            = -1;
        double  current_contour_area = std::numeric_limits<double>::max();
        for (ExPolygons::iterator slice = slices->begin(); slice != slices->end(); ++ slice) {
            if (slice->contour.contains(hole->points.front())) {
                double area = slice->contour.area();
                if (area < current_contour_area) {
                    slice_idx = slice - slices->begin();
                    current_contour_area = area;
                }
            }
        }
        // assert(slice_idx != -1);
        if (slice_idx == -1)
            // Ignore this hole.
            continue;
        assert(current_contour_area < std::numeric_limits<double>::max() && current_contour_area >= -hole->area());
        (*slices)[slice_idx].holes.emplace_back(std::move(*hole));
    }

#if 0
    // If the input mesh is not valid, the holes may intersect with the external contour.
    // Rather subtract them from the outer contour.
    Polygons poly;
    for (auto it_slice = slices->begin(); it_slice != slices->end(); ++ it_slice) {
        if (it_slice->holes.empty()) {
            poly.emplace_back(std::move(it_slice->contour));
        } else {
            Polygons contours;
            contours.emplace_back(std::move(it_slice->contour));
            for (auto it = it_slice->holes.begin(); it != it_slice->holes.end(); ++ it)
                it->reverse();
            polygons_append(poly, diff(contours, it_slice->holes));
        }
    }
    // If the input mesh is not valid, the input contours may intersect.
    *slices = union_ex(poly);
#endif

#if 0
    // If the input mesh is not valid, the holes may intersect with the external contour.
    // Rather subtract them from the outer contour.
    ExPolygons poly;
    for (auto it_slice = slices->begin(); it_slice != slices->end(); ++ it_slice) {
        Polygons contours;
        contours.emplace_back(std::move(it_slice->contour));
        for (auto it = it_slice->holes.begin(); it != it_slice->holes.end(); ++ it)
            it->reverse();
        expolygons_append(poly, diff_ex(contours, it_slice->holes));
    }
    // If the input mesh is not valid, the input contours may intersect.
    *slices = std::move(poly);
#endif
}

void TriangleMeshSlicer::make_expolygons(const Polygons &loops, const float closing_radius, ExPolygons* slices) const
{
    /*
        Input loops are not suitable for evenodd nor nonzero fill types, as we might get
        two consecutive concentric loops having the same winding order - and we have to 
        respect such order. In that case, evenodd would create wrong inversions, and nonzero
        would ignore holes inside two concentric contours.
        So we're ordering loops and collapse consecutive concentric loops having the same 
        winding order.
        TODO: find a faster algorithm for this, maybe with some sort of binary search.
        If we computed a "nesting tree" we could also just remove the consecutive loops
        having the same winding order, and remove the extra one(s) so that we could just
        supply everything to offset() instead of performing several union/diff calls.
    
        we sort by area assuming that the outermost loops have larger area;
        the previous sorting method, based on $b->contains($a->[0]), failed to nest
        loops correctly in some edge cases when original model had overlapping facets
    */

    /* The following lines are commented out because they can generate wrong polygons,
       see for example issue #661 */

    //std::vector<double> area;
    //std::vector<size_t> sorted_area;  // vector of indices
    //for (Polygons::const_iterator loop = loops.begin(); loop != loops.end(); ++ loop) {
    //    area.emplace_back(loop->area());
    //    sorted_area.emplace_back(loop - loops.begin());
    //}
    //
    //// outer first
    //std::sort(sorted_area.begin(), sorted_area.end(),
    //    [&area](size_t a, size_t b) { return std::abs(area[a]) > std::abs(area[b]); });

    //// we don't perform a safety offset now because it might reverse cw loops
    //Polygons p_slices;
    //for (std::vector<size_t>::const_iterator loop_idx = sorted_area.begin(); loop_idx != sorted_area.end(); ++ loop_idx) {
    //    /* we rely on the already computed area to determine the winding order
    //       of the loops, since the Orientation() function provided by Clipper
    //       would do the same, thus repeating the calculation */
    //    Polygons::const_iterator loop = loops.begin() + *loop_idx;
    //    if (area[*loop_idx] > +EPSILON)
    //        p_slices.emplace_back(*loop);
    //    else if (area[*loop_idx] < -EPSILON)
    //        //FIXME This is arbitrary and possibly very slow.
    //        // If the hole is inside a polygon, then there is no need to diff.
    //        // If the hole intersects a polygon boundary, then diff it, but then
    //        // there is no guarantee of an ordering of the loops.
    //        // Maybe we can test for the intersection before running the expensive diff algorithm?
    //        p_slices = diff(p_slices, *loop);
    //}

    // Perform a safety offset to merge very close facets (TODO: find test case for this)
    // 0.0499 comes from https://github.com/slic3r/Slic3r/issues/959
//    double safety_offset = scale_(0.0499);
    // 0.0001 is set to satisfy GH #520, #1029, #1364
    double safety_offset = scale_(closing_radius);

    /* The following line is commented out because it can generate wrong polygons,
       see for example issue #661 */
    //ExPolygons ex_slices = offset2_ex(p_slices, +safety_offset, -safety_offset);
    
    #ifdef SLIC3R_TRIANGLEMESH_DEBUG
    size_t holes_count = 0;
    for (ExPolygons::const_iterator e = ex_slices.begin(); e != ex_slices.end(); ++ e)
        holes_count += e->holes.size();
    printf("%zu surface(s) having %zu holes detected from %zu polylines\n",
        ex_slices.size(), holes_count, loops.size());
    #endif
    
    // append to the supplied collection
    if (safety_offset > 0)
        expolygons_append(*slices, offset2_ex(union_(loops, false), +safety_offset, -safety_offset));
    else
        expolygons_append(*slices, union_ex(loops, false));
}

void TriangleMeshSlicer::make_expolygons(std::vector<IntersectionLine> &lines, const float closing_radius, ExPolygons* slices) const
{
    Polygons pp;
    this->make_loops(lines, &pp);
    this->make_expolygons(pp, closing_radius, slices);
}

void TriangleMeshSlicer::cut(float z, TriangleMesh* upper, TriangleMesh* lower) const
{
    IntersectionLines upper_lines, lower_lines;
    
    BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::cut - slicing object";
    float scaled_z = scale_(z);
    for (uint32_t facet_idx = 0; facet_idx < this->mesh->stl.stats.number_of_facets; ++ facet_idx) {
        const stl_facet* facet = &this->mesh->stl.facet_start[facet_idx];
        
        // find facet extents
        float min_z = std::min(facet->vertex[0](2), std::min(facet->vertex[1](2), facet->vertex[2](2)));
        float max_z = std::max(facet->vertex[0](2), std::max(facet->vertex[1](2), facet->vertex[2](2)));
        
        // intersect facet with cutting plane
        IntersectionLine line;
        if (this->slice_facet(scaled_z, *facet, facet_idx, min_z, max_z, &line) != TriangleMeshSlicer::NoSlice) {
            // Save intersection lines for generating correct triangulations.
            if (line.edge_type == feTop) {
                lower_lines.emplace_back(line);
            } else if (line.edge_type == feBottom) {
                upper_lines.emplace_back(line);
            } else if (line.edge_type != feHorizontal) {
                lower_lines.emplace_back(line);
                upper_lines.emplace_back(line);
            }
        }
        
        if (min_z > z || (min_z == z && max_z > z)) {
            // facet is above the cut plane and does not belong to it
            if (upper != nullptr)
				stl_add_facet(&upper->stl, facet);
        } else if (max_z < z || (max_z == z && min_z < z)) {
            // facet is below the cut plane and does not belong to it
            if (lower != nullptr)
				stl_add_facet(&lower->stl, facet);
        } else if (min_z < z && max_z > z) {
            // Facet is cut by the slicing plane.

            // look for the vertex on whose side of the slicing plane there are no other vertices
            int isolated_vertex;
            if ( (facet->vertex[0](2) > z) == (facet->vertex[1](2) > z) ) {
                isolated_vertex = 2;
            } else if ( (facet->vertex[1](2) > z) == (facet->vertex[2](2) > z) ) {
                isolated_vertex = 0;
            } else {
                isolated_vertex = 1;
            }
            
            // get vertices starting from the isolated one
            const stl_vertex &v0 = facet->vertex[isolated_vertex];
            const stl_vertex &v1 = facet->vertex[(isolated_vertex+1) % 3];
            const stl_vertex &v2 = facet->vertex[(isolated_vertex+2) % 3];
            
            // intersect v0-v1 and v2-v0 with cutting plane and make new vertices
            stl_vertex v0v1, v2v0;
            v0v1(0) = v1(0) + (v0(0) - v1(0)) * (z - v1(2)) / (v0(2) - v1(2));
            v0v1(1) = v1(1) + (v0(1) - v1(1)) * (z - v1(2)) / (v0(2) - v1(2));
            v0v1(2) = z;
            v2v0(0) = v2(0) + (v0(0) - v2(0)) * (z - v2(2)) / (v0(2) - v2(2));
            v2v0(1) = v2(1) + (v0(1) - v2(1)) * (z - v2(2)) / (v0(2) - v2(2));
            v2v0(2) = z;
            
            // build the triangular facet
            stl_facet triangle;
            triangle.normal = facet->normal;
            triangle.vertex[0] = v0;
            triangle.vertex[1] = v0v1;
            triangle.vertex[2] = v2v0;
            
            // build the facets forming a quadrilateral on the other side
            stl_facet quadrilateral[2];
            quadrilateral[0].normal = facet->normal;
            quadrilateral[0].vertex[0] = v1;
            quadrilateral[0].vertex[1] = v2;
            quadrilateral[0].vertex[2] = v0v1;
            quadrilateral[1].normal = facet->normal;
            quadrilateral[1].vertex[0] = v2;
            quadrilateral[1].vertex[1] = v2v0;
            quadrilateral[1].vertex[2] = v0v1;
            
            if (v0(2) > z) {
                if (upper != nullptr) 
					stl_add_facet(&upper->stl, &triangle);
                if (lower != nullptr) {
                    stl_add_facet(&lower->stl, &quadrilateral[0]);
                    stl_add_facet(&lower->stl, &quadrilateral[1]);
                }
            } else {
                if (upper != nullptr) {
                    stl_add_facet(&upper->stl, &quadrilateral[0]);
                    stl_add_facet(&upper->stl, &quadrilateral[1]);
                }
                if (lower != nullptr) 
					stl_add_facet(&lower->stl, &triangle);
            }
        }
    }
    
    if (upper != nullptr) {
        BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::cut - triangulating upper part";
        ExPolygons section;
        this->make_expolygons_simple(upper_lines, &section);
        Pointf3s triangles = triangulate_expolygons_3d(section, z, true);
        stl_facet facet;
        facet.normal = stl_normal(0, 0, -1.f);
        for (size_t i = 0; i < triangles.size(); ) {
            for (size_t j = 0; j < 3; ++ j)
                facet.vertex[j] = triangles[i ++].cast<float>();
            stl_add_facet(&upper->stl, &facet);
        }
    }
    
    if (lower != nullptr) {
        BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::cut - triangulating lower part";
        ExPolygons section;
        this->make_expolygons_simple(lower_lines, &section);
        Pointf3s triangles = triangulate_expolygons_3d(section, z, false);
        stl_facet facet;
        facet.normal = stl_normal(0, 0, -1.f);
        for (size_t i = 0; i < triangles.size(); ) {
            for (size_t j = 0; j < 3; ++ j)
                facet.vertex[j] = triangles[i ++].cast<float>();
            stl_add_facet(&lower->stl, &facet);
        }
    }
    
    BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::cut - updating object sizes";
    stl_get_size(&upper->stl);
    stl_get_size(&lower->stl);
}

// Generate the vertex list for a cube solid of arbitrary size in X/Y/Z.
TriangleMesh make_cube(double x, double y, double z) 
{
    TriangleMesh mesh(
        {
            {x, y, 0}, {x, 0, 0}, {0, 0, 0},
            {0, y, 0}, {x, y, z}, {0, y, z},
            {0, 0, z}, {x, 0, z}
        },
        {
            {0, 1, 2}, {0, 2, 3}, {4, 5, 6},
            {4, 6, 7}, {0, 4, 7}, {0, 7, 1},
            {1, 7, 6}, {1, 6, 2}, {2, 6, 5},
            {2, 5, 3}, {4, 0, 3}, {4, 3, 5}
        });
	mesh.repair();
	return mesh;
}

// Generate the mesh for a cylinder and return it, using 
// the generated angle to calculate the top mesh triangles.
// Default is 360 sides, angle fa is in radians.
TriangleMesh make_cylinder(double r, double h, double fa)
{
	size_t n_steps    = (size_t)ceil(2. * PI / fa);
	double angle_step = 2. * PI / n_steps;

	Pointf3s			vertices;
	std::vector<Vec3i>	facets;
	vertices.reserve(2 * n_steps + 2);
	facets.reserve(4 * n_steps);

    // 2 special vertices, top and bottom center, rest are relative to this
    vertices.emplace_back(Vec3d(0.0, 0.0, 0.0));
    vertices.emplace_back(Vec3d(0.0, 0.0, h));

    // for each line along the polygon approximating the top/bottom of the
    // circle, generate four points and four facets (2 for the wall, 2 for the
    // top and bottom.
    // Special case: Last line shares 2 vertices with the first line.
	Vec2d p = Eigen::Rotation2Dd(0.) * Eigen::Vector2d(0, r);
	vertices.emplace_back(Vec3d(p(0), p(1), 0.));
	vertices.emplace_back(Vec3d(p(0), p(1), h));
	for (size_t i = 1; i < n_steps; ++i) {
        p = Eigen::Rotation2Dd(angle_step * i) * Eigen::Vector2d(0, r);
        vertices.emplace_back(Vec3d(p(0), p(1), 0.));
        vertices.emplace_back(Vec3d(p(0), p(1), h));
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
    
	TriangleMesh mesh(std::move(vertices), std::move(facets));
	mesh.repair();
	return mesh;
}

// Generates mesh for a sphere centered about the origin, using the generated angle
// to determine the granularity. 
// Default angle is 1 degree.
//FIXME better to discretize an Icosahedron recursively http://www.songho.ca/opengl/gl_sphere.html
TriangleMesh make_sphere(double radius, double fa)
{
	int   sectorCount = int(ceil(2. * M_PI / fa));
	int   stackCount  = int(ceil(M_PI / fa));
	float sectorStep  = float(2. * M_PI / sectorCount);
	float stackStep   = float(M_PI / stackCount);

	Pointf3s vertices;
	vertices.reserve((stackCount - 1) * sectorCount + 2);
	for (int i = 0; i <= stackCount; ++ i) {
		// from pi/2 to -pi/2
		double stackAngle = 0.5 * M_PI - stackStep * i;
		double xy = radius * cos(stackAngle);
		double z  = radius * sin(stackAngle);
		if (i == 0 || i == stackCount)
			vertices.emplace_back(Vec3d(xy, 0., z));
		else
			for (int j = 0; j < sectorCount; ++ j) {
				// from 0 to 2pi
				double sectorAngle = sectorStep * j;
				vertices.emplace_back(Vec3d(xy * cos(sectorAngle), xy * sin(sectorAngle), z));
			}
	}

	std::vector<Vec3i> facets;
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
	TriangleMesh mesh(std::move(vertices), std::move(facets));
	mesh.repair();
	return mesh;
}

}
