#include "TriangleMesh.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "qhull/src/libqhullcpp/Qhull.h"
#include "qhull/src/libqhullcpp/QhullFacetList.h"
#include "qhull/src/libqhullcpp/QhullVertexSet.h"
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

TriangleMesh::TriangleMesh()
    : repaired(false)
{
    stl_initialize(&this->stl);
}

TriangleMesh::TriangleMesh(const Pointf3s &points, const std::vector<Point3>& facets )
    : repaired(false)
{
    stl_initialize(&this->stl);
    stl_file &stl = this->stl;
    stl.error = 0;
    stl.stats.type = inmemory;

    // count facets and allocate memory
    stl.stats.number_of_facets = facets.size();
    stl.stats.original_num_facets = stl.stats.number_of_facets;
    stl_allocate(&stl);

    for (int i = 0; i < stl.stats.number_of_facets; i++) {
        stl_facet facet;

        const Pointf3& ref_f1 = points[facets[i].x];
        facet.vertex[0].x = ref_f1.x;
        facet.vertex[0].y = ref_f1.y;
        facet.vertex[0].z = ref_f1.z;

        const Pointf3& ref_f2 = points[facets[i].y];
        facet.vertex[1].x = ref_f2.x;
        facet.vertex[1].y = ref_f2.y;
        facet.vertex[1].z = ref_f2.z;

        const Pointf3& ref_f3 = points[facets[i].z];
        facet.vertex[2].x = ref_f3.x;
        facet.vertex[2].y = ref_f3.y;
        facet.vertex[2].z = ref_f3.z;
        
        facet.extra[0] = 0;
        facet.extra[1] = 0;

        float normal[3];
        stl_calculate_normal(normal, &facet);
        stl_normalize_vector(normal);
        facet.normal.x = normal[0];
        facet.normal.y = normal[1];
        facet.normal.z = normal[2];

        stl.facet_start[i] = facet;
    }
    stl_get_size(&stl);
}

TriangleMesh::TriangleMesh(const TriangleMesh &other) :
    repaired(false)
{
    stl_initialize(&this->stl);
    *this = other;
}

TriangleMesh::TriangleMesh(TriangleMesh &&other) : 
    repaired(false)
{
    stl_initialize(&this->stl);
    this->swap(other);
}

TriangleMesh& TriangleMesh::operator=(const TriangleMesh &other)
{
    stl_close(&this->stl);
    this->stl       = other.stl;
    this->repaired  = other.repaired;
    this->stl.heads = nullptr;
    this->stl.tail  = nullptr;
    this->stl.error = other.stl.error;
    if (other.stl.facet_start != nullptr) {
        this->stl.facet_start = (stl_facet*)calloc(other.stl.stats.number_of_facets, sizeof(stl_facet));
        std::copy(other.stl.facet_start, other.stl.facet_start + other.stl.stats.number_of_facets, this->stl.facet_start);
    }
    if (other.stl.neighbors_start != nullptr) {
        this->stl.neighbors_start = (stl_neighbors*)calloc(other.stl.stats.number_of_facets, sizeof(stl_neighbors));
        std::copy(other.stl.neighbors_start, other.stl.neighbors_start + other.stl.stats.number_of_facets, this->stl.neighbors_start);
    }
    if (other.stl.v_indices != nullptr) {
        this->stl.v_indices = (v_indices_struct*)calloc(other.stl.stats.number_of_facets, sizeof(v_indices_struct));
        std::copy(other.stl.v_indices, other.stl.v_indices + other.stl.stats.number_of_facets, this->stl.v_indices);
    }
    if (other.stl.v_shared != nullptr) {
        this->stl.v_shared = (stl_vertex*)calloc(other.stl.stats.shared_vertices, sizeof(stl_vertex));
        std::copy(other.stl.v_shared, other.stl.v_shared + other.stl.stats.shared_vertices, this->stl.v_shared);
    }
    return *this;
}

TriangleMesh& TriangleMesh::operator=(TriangleMesh &&other)
{
    this->swap(other);
    return *this;
}

void
TriangleMesh::swap(TriangleMesh &other)
{
    std::swap(this->stl,      other.stl);
    std::swap(this->repaired, other.repaired);
}

TriangleMesh::~TriangleMesh() {
    stl_close(&this->stl);
}

void
TriangleMesh::ReadSTLFile(const char* input_file) {
    stl_open(&stl, input_file);
}

void
TriangleMesh::write_ascii(const char* output_file)
{
    stl_write_ascii(&this->stl, output_file, "");
}

void
TriangleMesh::write_binary(const char* output_file)
{
    stl_write_binary(&this->stl, output_file, "");
}

void
TriangleMesh::repair() {
    if (this->repaired) return;
    
    // admesh fails when repairing empty meshes
    if (this->stl.stats.number_of_facets == 0) return;

    BOOST_LOG_TRIVIAL(debug) << "TriangleMesh::repair() started";
    
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
    if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
        for (int i = 0; i < iterations; i++) {
            if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
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
    
    // remove_unconnected
    if (stl.stats.connected_facets_3_edge <  stl.stats.number_of_facets) {
        stl_remove_unconnected_facets(&stl);
    }
    
    // fill_holes
    if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
        stl_fill_holes(&stl);
        stl_clear_error(&stl);
    }

    // normal_directions
    stl_fix_normal_directions(&stl);

    // normal_values
    stl_fix_normal_values(&stl);
    
    // always calculate the volume and reverse all normals if volume is negative
    stl_calculate_volume(&stl);
    
    // neighbors
    stl_verify_neighbors(&stl);

    this->repaired = true;

    BOOST_LOG_TRIVIAL(debug) << "TriangleMesh::repair() finished";
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
    if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
        for (int i = 0; i < iterations; i++) {
            if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
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

bool TriangleMesh::is_manifold() const
{
    return this->stl.stats.connected_facets_3_edge == this->stl.stats.number_of_facets;
}

void
TriangleMesh::reset_repair_stats() {
    this->stl.stats.degenerate_facets   = 0;
    this->stl.stats.edges_fixed         = 0;
    this->stl.stats.facets_removed      = 0;
    this->stl.stats.facets_added        = 0;
    this->stl.stats.facets_reversed     = 0;
    this->stl.stats.backwards_edges     = 0;
    this->stl.stats.normals_fixed       = 0;
}

bool
TriangleMesh::needed_repair() const
{
    return this->stl.stats.degenerate_facets    > 0
        || this->stl.stats.edges_fixed          > 0
        || this->stl.stats.facets_removed       > 0
        || this->stl.stats.facets_added         > 0
        || this->stl.stats.facets_reversed      > 0
        || this->stl.stats.backwards_edges      > 0;
}

size_t
TriangleMesh::facets_count() const
{
    return this->stl.stats.number_of_facets;
}

void
TriangleMesh::WriteOBJFile(char* output_file) {
    stl_generate_shared_vertices(&stl);
    stl_write_obj(&stl, output_file);
}

void TriangleMesh::scale(float factor)
{
    stl_scale(&(this->stl), factor);
    stl_invalidate_shared_vertices(&this->stl);
}

void TriangleMesh::scale(const Pointf3 &versor)
{
    float fversor[3];
    fversor[0] = versor.x;
    fversor[1] = versor.y;
    fversor[2] = versor.z;
    stl_scale_versor(&this->stl, fversor);
    stl_invalidate_shared_vertices(&this->stl);
}

void TriangleMesh::translate(float x, float y, float z)
{
    if (x == 0.f && y == 0.f && z == 0.f)
        return;
    stl_translate_relative(&(this->stl), x, y, z);
    stl_invalidate_shared_vertices(&this->stl);
}

void TriangleMesh::rotate(float angle, Pointf3 axis)
{
    if (angle == 0.f)
        return;

    axis = normalize(axis);
    Eigen::Transform<float, 3, Eigen::Affine> m = Eigen::Transform<float, 3, Eigen::Affine>::Identity();
    m.rotate(Eigen::AngleAxisf(angle, Eigen::Vector3f(axis.x, axis.y, axis.z)));
    stl_transform(&stl, (float*)m.data());
}

void TriangleMesh::rotate(float angle, const Axis &axis)
{
    if (angle == 0.f)
        return;

    // admesh uses degrees
    angle = Slic3r::Geometry::rad2deg(angle);
    
    if (axis == X) {
        stl_rotate_x(&(this->stl), angle);
    } else if (axis == Y) {
        stl_rotate_y(&(this->stl), angle);
    } else if (axis == Z) {
        stl_rotate_z(&(this->stl), angle);
    }
    stl_invalidate_shared_vertices(&this->stl);
}

void TriangleMesh::rotate_x(float angle)
{
    this->rotate(angle, X);
}

void TriangleMesh::rotate_y(float angle)
{
    this->rotate(angle, Y);
}

void TriangleMesh::rotate_z(float angle)
{
    this->rotate(angle, Z);
}

void TriangleMesh::mirror(const Axis &axis)
{
    if (axis == X) {
        stl_mirror_yz(&this->stl);
    } else if (axis == Y) {
        stl_mirror_xz(&this->stl);
    } else if (axis == Z) {
        stl_mirror_xy(&this->stl);
    }
    stl_invalidate_shared_vertices(&this->stl);
}

void TriangleMesh::mirror_x()
{
    this->mirror(X);
}

void TriangleMesh::mirror_y()
{
    this->mirror(Y);
}

void TriangleMesh::mirror_z()
{
    this->mirror(Z);
}

void TriangleMesh::transform(const float* matrix3x4)
{
    if (matrix3x4 == nullptr)
        return;

    stl_transform(&stl, const_cast<float*>(matrix3x4));
    stl_invalidate_shared_vertices(&stl);
}

void TriangleMesh::align_to_origin()
{
    this->translate(
        -(this->stl.stats.min.x),
        -(this->stl.stats.min.y),
        -(this->stl.stats.min.z)
    );
}

void TriangleMesh::rotate(double angle, Point* center)
{
    if (angle == 0.)
        return;
    this->translate(float(-center->x), float(-center->y), 0);
    stl_rotate_z(&(this->stl), (float)angle);
    this->translate(float(+center->x), float(+center->y), 0);
}

bool TriangleMesh::has_multiple_patches() const
{
    // we need neighbors
    if (!this->repaired) CONFESS("split() requires repair()");
    
    if (this->stl.stats.number_of_facets == 0)
        return false;

    std::vector<int>  facet_queue(this->stl.stats.number_of_facets, 0);
    std::vector<char> facet_visited(this->stl.stats.number_of_facets, false);
    int               facet_queue_cnt = 1;
    facet_queue[0] = 0;
    facet_visited[0] = true;
    while (facet_queue_cnt > 0) {
        int facet_idx = facet_queue[-- facet_queue_cnt];
        facet_visited[facet_idx] = true;
        for (int j = 0; j < 3; ++ j) {
            int neighbor_idx = this->stl.neighbors_start[facet_idx].neighbor[j];
            if (neighbor_idx != -1 && ! facet_visited[neighbor_idx])
                facet_queue[facet_queue_cnt ++] = neighbor_idx;
        }
    }

    // If any of the face was not visited at the first time, return "multiple bodies".
    for (int facet_idx = 0; facet_idx < this->stl.stats.number_of_facets; ++ facet_idx)
        if (! facet_visited[facet_idx])
            return true;
    return false;
}

size_t TriangleMesh::number_of_patches() const
{
    // we need neighbors
    if (!this->repaired) CONFESS("split() requires repair()");
    
    if (this->stl.stats.number_of_facets == 0)
        return false;

    std::vector<int>  facet_queue(this->stl.stats.number_of_facets, 0);
    std::vector<char> facet_visited(this->stl.stats.number_of_facets, false);
    int               facet_queue_cnt = 0;
    size_t            num_bodies = 0;
    for (;;) {
        // Find a seeding triangle for a new body.
        int facet_idx = 0;
        for (; facet_idx < this->stl.stats.number_of_facets; ++ facet_idx)
            if (! facet_visited[facet_idx]) {
                // A seed triangle was found.
                facet_queue[facet_queue_cnt ++] = facet_idx;
                facet_visited[facet_idx] = true;
                break;
            }
        if (facet_idx == this->stl.stats.number_of_facets)
            // No seed found.
            break;
        ++ num_bodies;
        while (facet_queue_cnt > 0) {
            int facet_idx = facet_queue[-- facet_queue_cnt];
            facet_visited[facet_idx] = true;
            for (int j = 0; j < 3; ++ j) {
                int neighbor_idx = this->stl.neighbors_start[facet_idx].neighbor[j];
                if (neighbor_idx != -1 && ! facet_visited[neighbor_idx])
                    facet_queue[facet_queue_cnt ++] = neighbor_idx;
            }
        }
    }

    return num_bodies;
}

TriangleMeshPtrs TriangleMesh::split() const
{
    TriangleMeshPtrs meshes;
    std::set<int> seen_facets;
    
    // we need neighbors
    if (!this->repaired) CONFESS("split() requires repair()");
    
    // loop while we have remaining facets
    for (;;) {
        // get the first facet
        std::queue<int> facet_queue;
        std::deque<int> facets;
        for (int facet_idx = 0; facet_idx < this->stl.stats.number_of_facets; facet_idx++) {
            if (seen_facets.find(facet_idx) == seen_facets.end()) {
                // if facet was not seen put it into queue and start searching
                facet_queue.push(facet_idx);
                break;
            }
        }
        if (facet_queue.empty()) break;
        
        while (!facet_queue.empty()) {
            int facet_idx = facet_queue.front();
            facet_queue.pop();
            if (seen_facets.find(facet_idx) != seen_facets.end()) continue;
            facets.push_back(facet_idx);
            for (int j = 0; j <= 2; j++) {
                facet_queue.push(this->stl.neighbors_start[facet_idx].neighbor[j]);
            }
            seen_facets.insert(facet_idx);
        }
        
        TriangleMesh* mesh = new TriangleMesh;
        meshes.push_back(mesh);
        mesh->stl.stats.type = inmemory;
        mesh->stl.stats.number_of_facets = facets.size();
        mesh->stl.stats.original_num_facets = mesh->stl.stats.number_of_facets;
        stl_clear_error(&mesh->stl);
        stl_allocate(&mesh->stl);
        
        int first = 1;
        for (std::deque<int>::const_iterator facet = facets.begin(); facet != facets.end(); ++facet) {
            mesh->stl.facet_start[facet - facets.begin()] = this->stl.facet_start[*facet];
            stl_facet_stats(&mesh->stl, this->stl.facet_start[*facet], first);
            first = 0;
        }
    }
    
    return meshes;
}

void TriangleMesh::merge(const TriangleMesh &mesh)
{
    // reset stats and metadata
    int number_of_facets = this->stl.stats.number_of_facets;
    stl_invalidate_shared_vertices(&this->stl);
    this->repaired = false;
    
    // update facet count and allocate more memory
    this->stl.stats.number_of_facets = number_of_facets + mesh.stl.stats.number_of_facets;
    this->stl.stats.original_num_facets = this->stl.stats.number_of_facets;
    stl_reallocate(&this->stl);
    
    // copy facets
    for (int i = 0; i < mesh.stl.stats.number_of_facets; i++) {
        this->stl.facet_start[number_of_facets + i] = mesh.stl.facet_start[i];
    }
    
    // update size
    stl_get_size(&this->stl);
}

// Calculate projection of the mesh into the XY plane, in scaled coordinates.
//FIXME This could be extremely slow! Use it for tiny meshes only!
ExPolygons TriangleMesh::horizontal_projection() const
{
    Polygons pp;
    pp.reserve(this->stl.stats.number_of_facets);
    for (int i = 0; i < this->stl.stats.number_of_facets; i++) {
        stl_facet* facet = &this->stl.facet_start[i];
        Polygon p;
        p.points.resize(3);
        p.points[0] = Point::new_scale(facet->vertex[0].x, facet->vertex[0].y);
        p.points[1] = Point::new_scale(facet->vertex[1].x, facet->vertex[1].y);
        p.points[2] = Point::new_scale(facet->vertex[2].x, facet->vertex[2].y);
        p.make_counter_clockwise();  // do this after scaling, as winding order might change while doing that
        pp.push_back(p);
    }
    
    // the offset factor was tuned using groovemount.stl
    return union_ex(offset(pp, scale_(0.01)), true);
}

Polygon TriangleMesh::convex_hull()
{
    this->require_shared_vertices();
    Points pp;
    pp.reserve(this->stl.stats.shared_vertices);
    for (int i = 0; i < this->stl.stats.shared_vertices; ++ i) {
        stl_vertex* v = &this->stl.v_shared[i];
        pp.emplace_back(Point::new_scale(v->x, v->y));
    }
    return Slic3r::Geometry::convex_hull(pp);
}

BoundingBoxf3 TriangleMesh::bounding_box() const
{
    BoundingBoxf3 bb;
    bb.defined = true;
    bb.min.x = this->stl.stats.min.x;
    bb.min.y = this->stl.stats.min.y;
    bb.min.z = this->stl.stats.min.z;
    bb.max.x = this->stl.stats.max.x;
    bb.max.y = this->stl.stats.max.y;
    bb.max.z = this->stl.stats.max.z;
    return bb;
}

BoundingBoxf3 TriangleMesh::transformed_bounding_box(const std::vector<float>& matrix) const
{
    bool has_shared = (stl.v_shared != nullptr);
    if (!has_shared)
        stl_generate_shared_vertices(&stl);

    unsigned int vertices_count = (stl.stats.shared_vertices > 0) ? (unsigned int)stl.stats.shared_vertices : 3 * (unsigned int)stl.stats.number_of_facets;

    if (vertices_count == 0)
        return BoundingBoxf3();

    Eigen::MatrixXf src_vertices(3, vertices_count);

    if (stl.stats.shared_vertices > 0)
    {
        stl_vertex* vertex_ptr = stl.v_shared;
        for (int i = 0; i < stl.stats.shared_vertices; ++i)
        {
            src_vertices(0, i) = vertex_ptr->x;
            src_vertices(1, i) = vertex_ptr->y;
            src_vertices(2, i) = vertex_ptr->z;
            vertex_ptr += 1;
        }
    }
    else
    {
        stl_facet* facet_ptr = stl.facet_start;
        unsigned int v_id = 0;
        while (facet_ptr < stl.facet_start + stl.stats.number_of_facets)
        {
            for (int i = 0; i < 3; ++i)
            {
                src_vertices(0, v_id) = facet_ptr->vertex[i].x;
                src_vertices(1, v_id) = facet_ptr->vertex[i].y;
                src_vertices(2, v_id) = facet_ptr->vertex[i].z;
            }
            facet_ptr += 1;
            ++v_id;
        }
    }

    if (!has_shared && (stl.stats.shared_vertices > 0))
        stl_invalidate_shared_vertices(&stl);

    Eigen::Transform<float, 3, Eigen::Affine> m;
    ::memcpy((void*)m.data(), (const void*)matrix.data(), 16 * sizeof(float));

    Eigen::MatrixXf dst_vertices(3, vertices_count);
    dst_vertices = m * src_vertices.colwise().homogeneous();

    float min_x = dst_vertices(0, 0);
    float max_x = dst_vertices(0, 0);
    float min_y = dst_vertices(1, 0);
    float max_y = dst_vertices(1, 0);
    float min_z = dst_vertices(2, 0);
    float max_z = dst_vertices(2, 0);

    for (int i = 1; i < vertices_count; ++i)
    {
        min_x = std::min(min_x, dst_vertices(0, i));
        max_x = std::max(max_x, dst_vertices(0, i));
        min_y = std::min(min_y, dst_vertices(1, i));
        max_y = std::max(max_y, dst_vertices(1, i));
        min_z = std::min(min_z, dst_vertices(2, i));
        max_z = std::max(max_z, dst_vertices(2, i));
    }

    return BoundingBoxf3(Pointf3((coordf_t)min_x, (coordf_t)min_y, (coordf_t)min_z), Pointf3((coordf_t)max_x, (coordf_t)max_y, (coordf_t)max_z));
}

TriangleMesh TriangleMesh::convex_hull_3d() const
{
    // Helper struct for qhull:
    struct PointForQHull{
        PointForQHull(float x_p, float y_p, float z_p) : x((realT)x_p), y((realT)y_p), z((realT)z_p) {}
        realT x, y, z;
    };
    std::vector<PointForQHull> src_vertices;

    // We will now fill the vector with input points for computation:
    stl_facet* facet_ptr = stl.facet_start;
    while (facet_ptr < stl.facet_start + stl.stats.number_of_facets)
    {
        for (int i = 0; i < 3; ++i)
        {
            const stl_vertex& v = facet_ptr->vertex[i];
            src_vertices.emplace_back(v.x, v.y, v.z);
        }

        facet_ptr += 1;
    }

    // The qhull call:
    orgQhull::Qhull qhull;
    qhull.disableOutputStream(); // we want qhull to be quiet
    try
    {
        qhull.runQhull("", 3, (int)src_vertices.size(), (const realT*)(src_vertices.data()), "Qt");
    }
    catch (...)
    {
        std::cout << "Unable to create convex hull" << std::endl;
        return TriangleMesh();
    }

    // Let's collect results:
    Pointf3s det_vertices;
    std::vector<Point3> facets;
    auto facet_list = qhull.facetList().toStdVector();
    for (const orgQhull::QhullFacet& facet : facet_list)
    {   // iterate through facets
        orgQhull::QhullVertexSet vertices = facet.vertices();
        for (int i = 0; i < 3; ++i)
        {   // iterate through facet's vertices

            orgQhull::QhullPoint p = vertices[i].point();
            const float* coords = p.coordinates();
            det_vertices.emplace_back(coords[0], coords[1], coords[2]);
        }
        unsigned int size = (unsigned int)det_vertices.size();
        facets.emplace_back(size - 3, size - 2, size - 1);
    }

    TriangleMesh output_mesh(det_vertices, facets);
    output_mesh.repair();
    output_mesh.require_shared_vertices();
    return output_mesh;
}

const float* TriangleMesh::first_vertex() const
{
    return stl.facet_start ? &stl.facet_start->vertex[0].x : nullptr;
}

void TriangleMesh::require_shared_vertices()
{
    BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::require_shared_vertices - start";
    if (!this->repaired) 
        this->repair();
    if (this->stl.v_shared == NULL) {
        BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::require_shared_vertices - stl_generate_shared_vertices";
        stl_generate_shared_vertices(&(this->stl));
    }
#ifdef _DEBUG
	// Verify validity of neighborship data.
	for (int facet_idx = 0; facet_idx < stl.stats.number_of_facets; ++facet_idx) {
		const stl_neighbors &nbr = stl.neighbors_start[facet_idx];
		const int *vertices = stl.v_indices[facet_idx].vertex;
		for (int nbr_idx = 0; nbr_idx < 3; ++nbr_idx) {
			int nbr_face = this->stl.neighbors_start[facet_idx].neighbor[nbr_idx];
			if (nbr_face != -1) {
				assert(stl.v_indices[nbr_face].vertex[(nbr.which_vertex_not[nbr_idx] + 1) % 3] == vertices[(nbr_idx + 1) % 3]);
				assert(stl.v_indices[nbr_face].vertex[(nbr.which_vertex_not[nbr_idx] + 2) % 3] == vertices[nbr_idx]);
			}
		}
	}
#endif /* _DEBUG */
    BOOST_LOG_TRIVIAL(trace) << "TriangleMeshSlicer::require_shared_vertices - end";
}

TriangleMeshSlicer::TriangleMeshSlicer(TriangleMesh* _mesh) : 
    mesh(_mesh)
{
    _mesh->require_shared_vertices();
    facets_edges.assign(_mesh->stl.stats.number_of_facets * 3, -1);
    v_scaled_shared.assign(_mesh->stl.v_shared, _mesh->stl.v_shared + _mesh->stl.stats.shared_vertices);
    // Scale the copied vertices.
    for (int i = 0; i < this->mesh->stl.stats.shared_vertices; ++ i) {
        this->v_scaled_shared[i].x /= float(SCALING_FACTOR);
        this->v_scaled_shared[i].y /= float(SCALING_FACTOR);
        this->v_scaled_shared[i].z /= float(SCALING_FACTOR);
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
    std::vector<EdgeToFace> edges_map;
    edges_map.assign(this->mesh->stl.stats.number_of_facets * 3, EdgeToFace());
    for (int facet_idx = 0; facet_idx < this->mesh->stl.stats.number_of_facets; ++ facet_idx)
        for (int i = 0; i < 3; ++ i) {
            EdgeToFace &e2f = edges_map[facet_idx*3+i];
            e2f.vertex_low  = this->mesh->stl.v_indices[facet_idx].vertex[i];
            e2f.vertex_high = this->mesh->stl.v_indices[facet_idx].vertex[(i + 1) % 3];
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
    }
}

void TriangleMeshSlicer::slice(const std::vector<float> &z, std::vector<Polygons>* layers) const
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
            [&lines, &lines_mutex, &z, this](const tbb::blocked_range<int>& range) {
                for (int facet_idx = range.begin(); facet_idx < range.end(); ++ facet_idx)
                    this->_slice_do(facet_idx, &lines, &lines_mutex, z);
            }
        );
    }
    
    // v_scaled_shared could be freed here
    
    // build loops
    BOOST_LOG_TRIVIAL(debug) << "TriangleMeshSlicer::_make_loops_do";
    layers->resize(z.size());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, z.size()),
        [&lines, &layers, this](const tbb::blocked_range<size_t>& range) {
            for (size_t line_idx = range.begin(); line_idx < range.end(); ++ line_idx)
                this->make_loops(lines[line_idx], &(*layers)[line_idx]);
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
    const stl_facet &facet = this->mesh->stl.facet_start[facet_idx];
    
    // find facet extents
    const float min_z = fminf(facet.vertex[0].z, fminf(facet.vertex[1].z, facet.vertex[2].z));
    const float max_z = fmaxf(facet.vertex[0].z, fmaxf(facet.vertex[1].z, facet.vertex[2].z));
    
    #ifdef SLIC3R_TRIANGLEMESH_DEBUG
    printf("\n==> FACET %d (%f,%f,%f - %f,%f,%f - %f,%f,%f):\n", facet_idx,
        facet.vertex[0].x, facet.vertex[0].y, facet.vertex[0].z,
        facet.vertex[1].x, facet.vertex[1].y, facet.vertex[1].z,
        facet.vertex[2].x, facet.vertex[2].y, facet.vertex[2].z);
    printf("z: min = %.2f, max = %.2f\n", min_z, max_z);
    #endif /* SLIC3R_TRIANGLEMESH_DEBUG */
    
    // find layer extents
    std::vector<float>::const_iterator min_layer, max_layer;
    min_layer = std::lower_bound(z.begin(), z.end(), min_z); // first layer whose slice_z is >= min_z
    max_layer = std::upper_bound(z.begin() + (min_layer - z.begin()), z.end(), max_z) - 1; // last layer whose slice_z is <= max_z
    #ifdef SLIC3R_TRIANGLEMESH_DEBUG
    printf("layers: min = %d, max = %d\n", (int)(min_layer - z.begin()), (int)(max_layer - z.begin()));
    #endif /* SLIC3R_TRIANGLEMESH_DEBUG */
    
    for (std::vector<float>::const_iterator it = min_layer; it != max_layer + 1; ++it) {
        std::vector<float>::size_type layer_idx = it - z.begin();
        IntersectionLine il;
        if (this->slice_facet(*it / SCALING_FACTOR, facet, facet_idx, min_z, max_z, &il) == TriangleMeshSlicer::Slicing) {
            boost::lock_guard<boost::mutex> l(*lines_mutex);
            if (il.edge_type == feHorizontal) {
                // Insert all marked edges of the face. The marked edges do not share an edge with another horizontal face
                // (they may not have a nighbor, or their neighbor is vertical)
                const int *vertices  = this->mesh->stl.v_indices[facet_idx].vertex;
                const bool reverse   = this->mesh->stl.facet_start[facet_idx].normal.z < 0;
                for (int j = 0; j < 3; ++ j)
                    if (il.flags & ((IntersectionLine::EDGE0_NO_NEIGHBOR | IntersectionLine::EDGE0_FOLD) << j)) {
                        int a_id = vertices[j % 3];
                        int b_id = vertices[(j+1) % 3];
                        if (reverse)
                            std::swap(a_id, b_id);
                        const stl_vertex *a = &this->v_scaled_shared[a_id];
                        const stl_vertex *b = &this->v_scaled_shared[b_id];
                        il.a.x    = a->x;
                        il.a.y    = a->y;
                        il.b.x    = b->x;
                        il.b.y    = b->y;
                        il.a_id   = a_id;
                        il.b_id   = b_id;
                        assert(il.a != il.b);
                        // This edge will not be used as a seed for loop extraction if it was added due to a fold of two overlapping horizontal faces.
                        il.set_no_seed((IntersectionLine::EDGE0_FOLD << j) != 0);
                        (*lines)[layer_idx].emplace_back(il);
                    }
            } else
                (*lines)[layer_idx].emplace_back(il);
        }
    }
}

void TriangleMeshSlicer::slice(const std::vector<float> &z, std::vector<ExPolygons>* layers) const
{
    std::vector<Polygons> layers_p;
    this->slice(z, &layers_p);
    
	BOOST_LOG_TRIVIAL(debug) << "TriangleMeshSlicer::make_expolygons in parallel - start";
	layers->resize(z.size());
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, z.size()),
		[&layers_p, layers, this](const tbb::blocked_range<size_t>& range) {
    		for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
#ifdef SLIC3R_TRIANGLEMESH_DEBUG
    			printf("Layer " PRINTF_ZU " (slice_z = %.2f):\n", layer_id, z[layer_id]);
#endif
    			this->make_expolygons(layers_p[layer_id], &(*layers)[layer_id]);
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
	const int *vertices = this->mesh->stl.v_indices[facet_idx].vertex;
    int i = (facet.vertex[1].z == min_z) ? 1 : ((facet.vertex[2].z == min_z) ? 2 : 0);
    for (int j = i; j - i < 3; ++j) {  // loop through facet edges
        int               edge_id  = this->facets_edges[facet_idx * 3 + (j % 3)];
        int               a_id     = vertices[j % 3];
        int               b_id     = vertices[(j+1) % 3];
        const stl_vertex *a = &this->v_scaled_shared[a_id];
        const stl_vertex *b = &this->v_scaled_shared[b_id];
        
        // Is edge or face aligned with the cutting plane?
        if (a->z == slice_z && b->z == slice_z) {
            // Edge is horizontal and belongs to the current layer.
            const stl_vertex &v0 = this->v_scaled_shared[vertices[0]];
            const stl_vertex &v1 = this->v_scaled_shared[vertices[1]];
            const stl_vertex &v2 = this->v_scaled_shared[vertices[2]];
            const stl_normal &normal = this->mesh->stl.facet_start[facet_idx].normal;
            // We may ignore this edge for slicing purposes, but we may still use it for object cutting.
            FacetSliceType    result = Slicing;
            const stl_neighbors &nbr = this->mesh->stl.neighbors_start[facet_idx];
            if (min_z == max_z) {
                // All three vertices are aligned with slice_z.
                line_out->edge_type = feHorizontal;
                // Mark neighbor edges, which do not have a neighbor.
                uint32_t edges = 0;
                for (int nbr_idx = 0; nbr_idx != 3; ++ nbr_idx) {
                    // If the neighbor with an edge starting with a vertex idx (nbr_idx - 2) shares no
                    // opposite face, add it to the edges to process when slicing.
					if (nbr.neighbor[nbr_idx] == -1) {
						// Mark this edge to be added to the slice.
						edges |= (IntersectionLine::EDGE0_NO_NEIGHBOR << nbr_idx);
					}
#if 1
                     else if (normal.z > 0) {
                        // Produce edges for opposite faced overlapping horizontal faces aka folds.
                        // This method often produces connecting lines (noise) at the cutting plane.
                        // Produce the edges for the top facing face of the pair of top / bottom facing faces.

                        // Index of a neighbor face.
                        const int  nbr_face     = nbr.neighbor[nbr_idx];
                        const int *nbr_vertices = this->mesh->stl.v_indices[nbr_face].vertex;
                        int idx_vertex_opposite = nbr_vertices[nbr.which_vertex_not[nbr_idx]];
                        const stl_vertex *c2 = &this->v_scaled_shared[idx_vertex_opposite];
                        if (c2->z == slice_z) {
                            // Edge shared by facet_idx and nbr_face.
                            int               a_id      = vertices[nbr_idx];
                            int               b_id      = vertices[(nbr_idx + 1) % 3];
                            int               c1_id     = vertices[(nbr_idx + 2) % 3];
                            const stl_vertex *a         = &this->v_scaled_shared[a_id];
                            const stl_vertex *b         = &this->v_scaled_shared[b_id];
                            const stl_vertex *c1        = &this->v_scaled_shared[c1_id];
                            // Verify that the two neighbor faces share a common edge.
                            assert(nbr_vertices[(nbr.which_vertex_not[nbr_idx] + 1) % 3] == b_id);
                            assert(nbr_vertices[(nbr.which_vertex_not[nbr_idx] + 2) % 3] == a_id);
                            double n1 = (double(c1->x) - double(a->x)) * (double(b->y) - double(a->y)) - (double(c1->y) - double(a->y)) * (double(b->x) - double(a->x));
                            double n2 = (double(c2->x) - double(a->x)) * (double(b->y) - double(a->y)) - (double(c2->y) - double(a->y)) * (double(b->x) - double(a->x));
                            if (n1 * n2 > 0)
                                // The two faces overlap. This indicates an invalid mesh geometry (non-manifold),
                                // but these are the real world objects, and leaving out these edges leads to missing contours.
                                edges |= (IntersectionLine::EDGE0_FOLD << nbr_idx);
                         }
                    }
#endif
                }
                // Use some edges of this triangle for slicing only if at least one of its edge does not have an opposite face.
                result = (edges == 0) ? Cutting : Slicing;
                line_out->flags |= edges;
                if (normal.z < 0) {
                    // If normal points downwards this is a bottom horizontal facet so we reverse its point order.
                    std::swap(a, b);
                    std::swap(a_id, b_id);
                }
            } else {
                // Two vertices are aligned with the cutting plane, the third vertex is below or above the cutting plane.
                int  nbr_idx     = j % 3;
                int  nbr_face    = nbr.neighbor[nbr_idx];
                // Is the third vertex below the cutting plane?
                bool third_below = v0.z < slice_z || v1.z < slice_z || v2.z < slice_z;
                // Is this a concave corner?
                if (nbr_face == -1) {
#ifdef _DEBUG
                    printf("Face has no neighbor!\n");
#endif
                } else {
                    assert(this->mesh->stl.v_indices[nbr_face].vertex[(nbr.which_vertex_not[nbr_idx] + 1) % 3] == b_id);
                    assert(this->mesh->stl.v_indices[nbr_face].vertex[(nbr.which_vertex_not[nbr_idx] + 2) % 3] == a_id);
                    int idx_vertex_opposite = this->mesh->stl.v_indices[nbr_face].vertex[nbr.which_vertex_not[nbr_idx]];
                    const stl_vertex *c = &this->v_scaled_shared[idx_vertex_opposite];
                    if (c->z == slice_z) {
                        double normal_nbr = (double(c->x) - double(a->x)) * (double(b->y) - double(a->y)) - (double(c->y) - double(a->y)) * (double(b->x) - double(a->x));
#if 0
                        if ((normal_nbr < 0) == third_below) {
                            printf("Flipped normal?\n");
                        }
#endif
                        result =
                                // A vertical face shares edge with a horizontal face. Verify, whether the shared edge makes a convex or concave corner.
                                // Unfortunately too often there are flipped normals, which brake our assumption. Let's rather return every edge,
                                // and leth the code downstream hopefully handle it.
    #if 1
                                // Ignore concave corners for slicing.
                                // This method has the unfortunate property, that folds in a horizontal plane create concave corners,
                                // leading to broken contours, if these concave corners are not replaced by edges of the folds, see above.
                                   ((normal_nbr < 0) == third_below) ? Cutting : Slicing;
    #else
                                // Use concave corners for slicing. This leads to the test 01_trianglemesh.t "slicing a top tangent plane includes its area" failing,
                                // and rightly so.
                                    Slicing;
    #endif
                    } else {
                        // For a pair of faces touching exactly at the cutting plane, ignore one of them. An arbitrary rule is to ignore the face with a higher index.
                        result = (facet_idx < nbr_face) ? Slicing : Cutting;
                    }
                }
                if (third_below) {
                    line_out->edge_type = feTop;
                    std::swap(a, b);
                    std::swap(a_id, b_id);
                } else
                    line_out->edge_type = feBottom;
            }
            line_out->a.x    = a->x;
            line_out->a.y    = a->y;
            line_out->b.x    = b->x;
            line_out->b.y    = b->y;
            line_out->a_id   = a_id;
            line_out->b_id   = b_id;
            assert(line_out->a != line_out->b);
            return result;
        }

        if (a->z == slice_z) {
            // Only point a alings with the cutting plane.
            if (point_on_layer == size_t(-1) || points[point_on_layer].point_id != a_id) {
                point_on_layer = num_points;
                IntersectionPoint &point = points[num_points ++];
                point.x         = a->x;
                point.y         = a->y;
                point.point_id  = a_id;
            }
        } else if (b->z == slice_z) {
            // Only point b alings with the cutting plane.
            if (point_on_layer == size_t(-1) || points[point_on_layer].point_id != b_id) {
                point_on_layer = num_points;
                IntersectionPoint &point = points[num_points ++];
                point.x         = b->x;
                point.y         = b->y;
                point.point_id  = b_id;
            }
        } else if ((a->z < slice_z && b->z > slice_z) || (b->z < slice_z && a->z > slice_z)) {
            // A general case. The face edge intersects the cutting plane. Calculate the intersection point.
			assert(a_id != b_id);
            // Sort the edge to give a consistent answer.
			if (a_id > b_id) {
				std::swap(a_id, b_id);
				std::swap(a, b);
			}
            IntersectionPoint &point = points[num_points];
            double t = (double(slice_z) - double(b->z)) / (double(a->z) - double(b->z));
			if (t <= 0.) {
				if (point_on_layer == size_t(-1) || points[point_on_layer].point_id != a_id) {
					point.x = a->x;
					point.y = a->y;
					point_on_layer = num_points ++;
					point.point_id = a_id;
				}
			} else if (t >= 1.) {
				if (point_on_layer == size_t(-1) || points[point_on_layer].point_id != b_id) {
					point.x = b->x;
					point.y = b->y;
					point_on_layer = num_points ++;
					point.point_id = b_id;
				}
			} else {
				point.x = coord_t(floor(double(b->x) + (double(a->x) - double(b->x)) * t + 0.5));
				point.y = coord_t(floor(double(b->y) + (double(a->y) - double(b->y)) * t + 0.5));
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
            line_out->edge_type = (this->v_scaled_shared[i].z < slice_z) ? feTop : feBottom;
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

void TriangleMeshSlicer::make_loops(std::vector<IntersectionLine> &lines, Polygons* loops) const
{
#if 0
//FIXME slice_facet() may create zero length edges due to rounding of doubles into coord_t.
//#ifdef _DEBUG
    for (const Line &l : lines)
        assert(l.a != l.b);
#endif /* _DEBUG */

    remove_tangent_edges(lines);

    struct OpenPolyline {
        OpenPolyline() {};
        OpenPolyline(const IntersectionReference &start, const IntersectionReference &end, Points &&points) : 
            start(start), end(end), points(std::move(points)), consumed(false) {}
        void reverse() {
            std::swap(start, end);
            std::reverse(points.begin(), points.end());
        }
        IntersectionReference   start;
        IntersectionReference   end;
        Points                  points;
        bool                    consumed;
    };
    std::vector<OpenPolyline> open_polylines;
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
                        loops->emplace_back(std::move(loop_pts));
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

    // Now process the open polylines.
    if (! open_polylines.empty()) {
        // Store the end points of open_polylines into vectors sorted
        struct OpenPolylineEnd {
            OpenPolylineEnd(OpenPolyline *polyline, bool start) : polyline(polyline), start(start) {}
            OpenPolyline    *polyline;
            // Is it the start or end point?
            bool             start;
            const IntersectionReference& ipref() const { return start ? polyline->start : polyline->end; }
            int point_id() const { return ipref().point_id; }
            int edge_id () const { return ipref().edge_id; }
        };
        auto by_edge_lower = [](const OpenPolylineEnd &ope1, const OpenPolylineEnd &ope2) { return ope1.edge_id() < ope2.edge_id(); };
        auto by_point_lower = [](const OpenPolylineEnd &ope1, const OpenPolylineEnd &ope2) { return ope1.point_id() < ope2.point_id(); };
        std::vector<OpenPolylineEnd> by_edge_id;
        std::vector<OpenPolylineEnd> by_point_id;
        by_edge_id.reserve(2 * open_polylines.size());
        by_point_id.reserve(2 * open_polylines.size());
        for (OpenPolyline &opl : open_polylines) {
            if (opl.start.edge_id != -1)
                by_edge_id .emplace_back(OpenPolylineEnd(&opl, true));
            if (opl.end.edge_id != -1)
                by_edge_id .emplace_back(OpenPolylineEnd(&opl, false));
            if (opl.start.point_id != -1)
                by_point_id.emplace_back(OpenPolylineEnd(&opl, true));
            if (opl.end.point_id != -1)
                by_point_id.emplace_back(OpenPolylineEnd(&opl, false));
        }
        std::sort(by_edge_id .begin(), by_edge_id .end(), by_edge_lower);
        std::sort(by_point_id.begin(), by_point_id.end(), by_point_lower);

        // Try to connect the loops.
        for (OpenPolyline &opl : open_polylines) {
            if (opl.consumed)
                continue;
            opl.consumed = true;
            OpenPolylineEnd end(&opl, false);
            for (;;) {
                // find a line starting where last one finishes
                OpenPolylineEnd* next_start = nullptr;
                if (end.edge_id() != -1) {
                    auto it_begin = std::lower_bound(by_edge_id.begin(), by_edge_id.end(), end, by_edge_lower);
                    if (it_begin != by_edge_id.end()) {
                        auto it_end = std::upper_bound(it_begin, by_edge_id.end(), end, by_edge_lower);
                        for (auto it_edge = it_begin; it_edge != it_end; ++ it_edge)
                            if (! it_edge->polyline->consumed) {
                                next_start = &(*it_edge);
                                break;
                            }
                    }
                }
                if (next_start == nullptr && end.point_id() != -1) {
                    auto it_begin = std::lower_bound(by_point_id.begin(), by_point_id.end(), end, by_point_lower);
                    if (it_begin != by_point_id.end()) {
                        auto it_end = std::upper_bound(it_begin, by_point_id.end(), end, by_point_lower);
                        for (auto it_point = it_begin; it_point != it_end; ++ it_point)
                            if (! it_point->polyline->consumed) {
                                next_start = &(*it_point);
                                break;
                            }
                    }
                }
				if (next_start == nullptr) {
					// The current loop could not be closed. Unmark the segment.
					opl.consumed = false;
					break;
				}
				// Attach this polyline to the end of the initial polyline.
                if (next_start->start) {
                    auto it = next_start->polyline->points.begin();
                    std::copy(++ it, next_start->polyline->points.end(), back_inserter(opl.points));
                    //opl.points.insert(opl.points.back(), ++ it, next_start->polyline->points.end());
                } else {
                    auto it = next_start->polyline->points.rbegin();
                    std::copy(++ it, next_start->polyline->points.rend(), back_inserter(opl.points));
                    //opl.points.insert(opl.points.back(), ++ it, next_start->polyline->points.rend());
                }
                end = *next_start;
                end.start = !end.start;
                next_start->polyline->points.clear();
                next_start->polyline->consumed = true;
                // Check whether we closed this loop.
                const IntersectionReference &ip1 = opl.start;
                const IntersectionReference &ip2 = end.ipref();
                if ((ip1.edge_id  != -1 && ip1.edge_id  == ip2.edge_id) ||
                    (ip1.point_id != -1 && ip1.point_id == ip2.point_id)) {
                    // The current loop is complete. Add it to the output.
                    //assert(opl.points.front().point_id == opl.points.back().point_id);
                    //assert(opl.points.front().edge_id  == opl.points.back().edge_id);
                    // Remove the duplicate last point.
                    opl.points.pop_back();
                    if (opl.points.size() >= 3) {
                        // The closed polygon is patched from pieces with messed up orientation, therefore
                        // the orientation of the patched up polygon is not known.
                        // Orient the patched up polygons CCW. This heuristic may close some holes and cavities.
                        double area = 0.;
                        for (size_t i = 0, j = opl.points.size() - 1; i < opl.points.size(); j = i ++)
                            area += double(opl.points[j].x + opl.points[i].x) * double(opl.points[i].y - opl.points[j].y);
                        if (area < 0)
                            std::reverse(opl.points.begin(), opl.points.end());
                        loops->emplace_back(std::move(opl.points));
                    }
                    opl.points.clear();
					break;
                }
				// Continue with the current loop.
            }
        }
    }
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
            slices->push_back(ex);
        } else {
            holes.push_back(*loop);
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

void TriangleMeshSlicer::make_expolygons(const Polygons &loops, ExPolygons* slices) const
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
    //    area.push_back(loop->area());
    //    sorted_area.push_back(loop - loops.begin());
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
    //        p_slices.push_back(*loop);
    //    else if (area[*loop_idx] < -EPSILON)
    //        //FIXME This is arbitrary and possibly very slow.
    //        // If the hole is inside a polygon, then there is no need to diff.
    //        // If the hole intersects a polygon boundary, then diff it, but then
    //        // there is no guarantee of an ordering of the loops.
    //        // Maybe we can test for the intersection before running the expensive diff algorithm?
    //        p_slices = diff(p_slices, *loop);
    //}

    // perform a safety offset to merge very close facets (TODO: find test case for this)
    double safety_offset = scale_(0.0499);
//FIXME see https://github.com/prusa3d/Slic3r/issues/520
//    double safety_offset = scale_(0.0001);

    /* The following line is commented out because it can generate wrong polygons,
       see for example issue #661 */
    //ExPolygons ex_slices = offset2_ex(p_slices, +safety_offset, -safety_offset);
    
    #ifdef SLIC3R_TRIANGLEMESH_DEBUG
    size_t holes_count = 0;
    for (ExPolygons::const_iterator e = ex_slices.begin(); e != ex_slices.end(); ++ e)
        holes_count += e->holes.size();
    printf(PRINTF_ZU " surface(s) having " PRINTF_ZU " holes detected from " PRINTF_ZU " polylines\n",
        ex_slices.size(), holes_count, loops.size());
    #endif
    
    // append to the supplied collection
    /* Fix for issue #661 { */
    expolygons_append(*slices, offset2_ex(union_(loops, false), +safety_offset, -safety_offset));
    //expolygons_append(*slices, ex_slices);
    /* } */
}

void TriangleMeshSlicer::make_expolygons(std::vector<IntersectionLine> &lines, ExPolygons* slices) const
{
    Polygons pp;
    this->make_loops(lines, &pp);
    this->make_expolygons(pp, slices);
}

void TriangleMeshSlicer::cut(float z, TriangleMesh* upper, TriangleMesh* lower) const
{
    IntersectionLines upper_lines, lower_lines;
    
    float scaled_z = scale_(z);
    for (int facet_idx = 0; facet_idx < this->mesh->stl.stats.number_of_facets; ++ facet_idx) {
        stl_facet* facet = &this->mesh->stl.facet_start[facet_idx];
        
        // find facet extents
        float min_z = std::min(facet->vertex[0].z, std::min(facet->vertex[1].z, facet->vertex[2].z));
        float max_z = std::max(facet->vertex[0].z, std::max(facet->vertex[1].z, facet->vertex[2].z));
        
        // intersect facet with cutting plane
        IntersectionLine line;
        if (this->slice_facet(scaled_z, *facet, facet_idx, min_z, max_z, &line) != TriangleMeshSlicer::NoSlice) {
            // Save intersection lines for generating correct triangulations.
            if (line.edge_type == feTop) {
                lower_lines.push_back(line);
            } else if (line.edge_type == feBottom) {
                upper_lines.push_back(line);
            } else if (line.edge_type != feHorizontal) {
                lower_lines.push_back(line);
                upper_lines.push_back(line);
            }
        }
        
        if (min_z > z || (min_z == z && max_z > z)) {
            // facet is above the cut plane and does not belong to it
            if (upper != NULL) stl_add_facet(&upper->stl, facet);
        } else if (max_z < z || (max_z == z && min_z < z)) {
            // facet is below the cut plane and does not belong to it
            if (lower != NULL) stl_add_facet(&lower->stl, facet);
        } else if (min_z < z && max_z > z) {
            // Facet is cut by the slicing plane.

            // look for the vertex on whose side of the slicing plane there are no other vertices
            int isolated_vertex;
            if ( (facet->vertex[0].z > z) == (facet->vertex[1].z > z) ) {
                isolated_vertex = 2;
            } else if ( (facet->vertex[1].z > z) == (facet->vertex[2].z > z) ) {
                isolated_vertex = 0;
            } else {
                isolated_vertex = 1;
            }
            
            // get vertices starting from the isolated one
            stl_vertex* v0 = &facet->vertex[isolated_vertex];
            stl_vertex* v1 = &facet->vertex[(isolated_vertex+1) % 3];
            stl_vertex* v2 = &facet->vertex[(isolated_vertex+2) % 3];
            
            // intersect v0-v1 and v2-v0 with cutting plane and make new vertices
            stl_vertex v0v1, v2v0;
            v0v1.x = v1->x + (v0->x - v1->x) * (z - v1->z) / (v0->z - v1->z);
            v0v1.y = v1->y + (v0->y - v1->y) * (z - v1->z) / (v0->z - v1->z);
            v0v1.z = z;
            v2v0.x = v2->x + (v0->x - v2->x) * (z - v2->z) / (v0->z - v2->z);
            v2v0.y = v2->y + (v0->y - v2->y) * (z - v2->z) / (v0->z - v2->z);
            v2v0.z = z;
            
            // build the triangular facet
            stl_facet triangle;
            triangle.normal = facet->normal;
            triangle.vertex[0] = *v0;
            triangle.vertex[1] = v0v1;
            triangle.vertex[2] = v2v0;
            
            // build the facets forming a quadrilateral on the other side
            stl_facet quadrilateral[2];
            quadrilateral[0].normal = facet->normal;
            quadrilateral[0].vertex[0] = *v1;
            quadrilateral[0].vertex[1] = *v2;
            quadrilateral[0].vertex[2] = v0v1;
            quadrilateral[1].normal = facet->normal;
            quadrilateral[1].vertex[0] = *v2;
            quadrilateral[1].vertex[1] = v2v0;
            quadrilateral[1].vertex[2] = v0v1;
            
            if (v0->z > z) {
                if (upper != NULL) stl_add_facet(&upper->stl, &triangle);
                if (lower != NULL) {
                    stl_add_facet(&lower->stl, &quadrilateral[0]);
                    stl_add_facet(&lower->stl, &quadrilateral[1]);
                }
            } else {
                if (upper != NULL) {
                    stl_add_facet(&upper->stl, &quadrilateral[0]);
                    stl_add_facet(&upper->stl, &quadrilateral[1]);
                }
                if (lower != NULL) stl_add_facet(&lower->stl, &triangle);
            }
        }
    }
    
    // triangulate holes of upper mesh
    if (upper != NULL) {
        // compute shape of section
        ExPolygons section;
        this->make_expolygons_simple(upper_lines, &section);
        
        // triangulate section
        Polygons triangles;
        for (ExPolygons::const_iterator expolygon = section.begin(); expolygon != section.end(); ++expolygon)
            expolygon->triangulate_p2t(&triangles);
        
        // convert triangles to facets and append them to mesh
        for (Polygons::const_iterator polygon = triangles.begin(); polygon != triangles.end(); ++polygon) {
            Polygon p = *polygon;
            p.reverse();
            stl_facet facet;
            facet.normal.x = 0;
            facet.normal.y = 0;
            facet.normal.z = -1;
            for (size_t i = 0; i <= 2; ++i) {
                facet.vertex[i].x = unscale(p.points[i].x);
                facet.vertex[i].y = unscale(p.points[i].y);
                facet.vertex[i].z = z;
            }
            stl_add_facet(&upper->stl, &facet);
        }
    }
    
    // triangulate holes of lower mesh
    if (lower != NULL) {
        // compute shape of section
        ExPolygons section;
        this->make_expolygons_simple(lower_lines, &section);
        
        // triangulate section
        Polygons triangles;
        for (ExPolygons::const_iterator expolygon = section.begin(); expolygon != section.end(); ++expolygon)
            expolygon->triangulate_p2t(&triangles);
        
        // convert triangles to facets and append them to mesh
        for (Polygons::const_iterator polygon = triangles.begin(); polygon != triangles.end(); ++polygon) {
            stl_facet facet;
            facet.normal.x = 0;
            facet.normal.y = 0;
            facet.normal.z = 1;
            for (size_t i = 0; i <= 2; ++i) {
                facet.vertex[i].x = unscale(polygon->points[i].x);
                facet.vertex[i].y = unscale(polygon->points[i].y);
                facet.vertex[i].z = z;
            }
            stl_add_facet(&lower->stl, &facet);
        }
    }
    
    // Update the bounding box / sphere of the new meshes.
    stl_get_size(&upper->stl);
    stl_get_size(&lower->stl);
}

// Generate the vertex list for a cube solid of arbitrary size in X/Y/Z.
TriangleMesh make_cube(double x, double y, double z) {
    Pointf3 pv[8] = { 
        Pointf3(x, y, 0), Pointf3(x, 0, 0), Pointf3(0, 0, 0), 
        Pointf3(0, y, 0), Pointf3(x, y, z), Pointf3(0, y, z), 
        Pointf3(0, 0, z), Pointf3(x, 0, z) 
    };
    Point3 fv[12] = { 
        Point3(0, 1, 2), Point3(0, 2, 3), Point3(4, 5, 6), 
        Point3(4, 6, 7), Point3(0, 4, 7), Point3(0, 7, 1), 
        Point3(1, 7, 6), Point3(1, 6, 2), Point3(2, 6, 5), 
        Point3(2, 5, 3), Point3(4, 0, 3), Point3(4, 3, 5) 
    };

    std::vector<Point3> facets(&fv[0], &fv[0]+12);
    Pointf3s vertices(&pv[0], &pv[0]+8);

    TriangleMesh mesh(vertices ,facets);
    return mesh;
}

// Generate the mesh for a cylinder and return it, using 
// the generated angle to calculate the top mesh triangles.
// Default is 360 sides, angle fa is in radians.
TriangleMesh make_cylinder(double r, double h, double fa) {
    Pointf3s vertices;
    std::vector<Point3> facets;

    // 2 special vertices, top and bottom center, rest are relative to this
    vertices.push_back(Pointf3(0.0, 0.0, 0.0));
    vertices.push_back(Pointf3(0.0, 0.0, h));

    // adjust via rounding to get an even multiple for any provided angle.
    double angle = (2*PI / floor(2*PI / fa));

    // for each line along the polygon approximating the top/bottom of the
    // circle, generate four points and four facets (2 for the wall, 2 for the
    // top and bottom.
    // Special case: Last line shares 2 vertices with the first line.
    unsigned id = vertices.size() - 1;
    vertices.push_back(Pointf3(sin(0) * r , cos(0) * r, 0));
    vertices.push_back(Pointf3(sin(0) * r , cos(0) * r, h));
    for (double i = 0; i < 2*PI; i+=angle) {
        Pointf3 b(0, r, 0);
        Pointf3 t(0, r, h);
        b.rotate(i, Pointf3(0,0,0)); 
        t.rotate(i, Pointf3(0,0,h));
        vertices.push_back(b);
        vertices.push_back(t);
        id = vertices.size() - 1;
        facets.push_back(Point3( 0, id - 1, id - 3)); // top
        facets.push_back(Point3(id,      1, id - 2)); // bottom
        facets.push_back(Point3(id, id - 2, id - 3)); // upper-right of side
        facets.push_back(Point3(id, id - 3, id - 1)); // bottom-left of side
    }
    // Connect the last set of vertices with the first.
    facets.push_back(Point3( 2, 0, id - 1));
    facets.push_back(Point3( 1, 3,     id));
    facets.push_back(Point3(id, 3,      2));
    facets.push_back(Point3(id, 2, id - 1));
    
    TriangleMesh mesh(vertices, facets);
    return mesh;
}

// Generates mesh for a sphere centered about the origin, using the generated angle
// to determine the granularity. 
// Default angle is 1 degree.
TriangleMesh make_sphere(double rho, double fa) {
    Pointf3s vertices;
    std::vector<Point3> facets;

    // Algorithm: 
    // Add points one-by-one to the sphere grid and form facets using relative coordinates.
    // Sphere is composed effectively of a mesh of stacked circles.

    // adjust via rounding to get an even multiple for any provided angle.
    double angle = (2*PI / floor(2*PI / fa));

    // Ring to be scaled to generate the steps of the sphere
    std::vector<double> ring;
    for (double i = 0; i < 2*PI; i+=angle) {
        ring.push_back(i);
    }
    const size_t steps = ring.size(); 
    const double increment = (double)(1.0 / (double)steps);

    // special case: first ring connects to 0,0,0
    // insert and form facets.
    vertices.push_back(Pointf3(0.0, 0.0, -rho));
    size_t id = vertices.size();
    for (size_t i = 0; i < ring.size(); i++) {
        // Fixed scaling 
        const double z = -rho + increment*rho*2.0;
        // radius of the circle for this step.
        const double r = sqrt(abs(rho*rho - z*z));
        Pointf3 b(0, r, z);
        b.rotate(ring[i], Pointf3(0,0,z)); 
        vertices.push_back(b);
        if (i == 0) {
            facets.push_back(Point3(1, 0, ring.size()));
        } else {
            facets.push_back(Point3(id, 0, id - 1));
        }
        id++;
    }

    // General case: insert and form facets for each step, joining it to the ring below it.
    for (size_t s = 2; s < steps - 1; s++) {
        const double z = -rho + increment*(double)s*2.0*rho;
        const double r = sqrt(abs(rho*rho - z*z));

        for (size_t i = 0; i < ring.size(); i++) {
            Pointf3 b(0, r, z);
            b.rotate(ring[i], Pointf3(0,0,z)); 
            vertices.push_back(b);
            if (i == 0) {
                // wrap around
                facets.push_back(Point3(id + ring.size() - 1 , id, id - 1)); 
                facets.push_back(Point3(id, id - ring.size(),  id - 1)); 
            } else {
                facets.push_back(Point3(id , id - ring.size(), (id - 1) - ring.size())); 
                facets.push_back(Point3(id, id - 1 - ring.size() ,  id - 1)); 
            }
            id++;
        } 
    }


    // special case: last ring connects to 0,0,rho*2.0
    // only form facets.
    vertices.push_back(Pointf3(0.0, 0.0, rho));
    for (size_t i = 0; i < ring.size(); i++) {
        if (i == 0) {
            // third vertex is on the other side of the ring.
            facets.push_back(Point3(id, id - ring.size(),  id - 1));
        } else {
            facets.push_back(Point3(id, id - ring.size() + i,  id - ring.size() + (i - 1)));
        }
    }
    id++;
    TriangleMesh mesh(vertices, facets);
    return mesh;
}
}
