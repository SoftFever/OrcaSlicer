#ifndef slic3r_TriangleMesh_hpp_
#define slic3r_TriangleMesh_hpp_

#include "libslic3r.h"
#include <admesh/stl.h>
#include <functional>
#include <vector>
#include "BoundingBox.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "ExPolygon.hpp"

namespace Slic3r {

class TriangleMesh;
class TriangleMeshSlicer;
typedef std::vector<TriangleMesh*> TriangleMeshPtrs;

class TriangleMesh
{
public:
    TriangleMesh() : repaired(false) {}
    TriangleMesh(const Pointf3s &points, const std::vector<Vec3i> &facets);
    explicit TriangleMesh(const indexed_triangle_set &M);
    void clear() { this->stl.clear(); this->its.clear(); this->repaired = false; }
    bool ReadSTLFile(const char* input_file) { return stl_open(&stl, input_file); }
    bool write_ascii(const char* output_file) { return stl_write_ascii(&this->stl, output_file, ""); }
    bool write_binary(const char* output_file) { return stl_write_binary(&this->stl, output_file, ""); }
    void repair(bool update_shared_vertices = true);
    float volume();
    void check_topology();
    bool is_manifold() const { return this->stl.stats.connected_facets_3_edge == (int)this->stl.stats.number_of_facets; }
    void WriteOBJFile(const char* output_file) const;
    void scale(float factor);
    void scale(const Vec3d &versor);
    void translate(float x, float y, float z);
    void translate(const Vec3f &displacement);
    void rotate(float angle, const Axis &axis);
    void rotate(float angle, const Vec3d& axis);
    void rotate_x(float angle) { this->rotate(angle, X); }
    void rotate_y(float angle) { this->rotate(angle, Y); }
    void rotate_z(float angle) { this->rotate(angle, Z); }
    void mirror(const Axis &axis);
    void mirror_x() { this->mirror(X); }
    void mirror_y() { this->mirror(Y); }
    void mirror_z() { this->mirror(Z); }
    void transform(const Transform3d& t, bool fix_left_handed = false);
    void transform(const Matrix3d& t, bool fix_left_handed = false);
    void align_to_origin();
    void rotate(double angle, Point* center);
    TriangleMeshPtrs split() const;
    void merge(const TriangleMesh &mesh);
    ExPolygons horizontal_projection() const;
    const float* first_vertex() const { return this->stl.facet_start.empty() ? nullptr : &this->stl.facet_start.front().vertex[0](0); }
    // 2D convex hull of a 3D mesh projected into the Z=0 plane.
    Polygon convex_hull();
    BoundingBoxf3 bounding_box() const;
    // Returns the bbox of this TriangleMesh transformed by the given transformation
    BoundingBoxf3 transformed_bounding_box(const Transform3d &trafo) const;
    // Return the size of the mesh in coordinates.
    Vec3d size() const { return stl.stats.size.cast<double>(); }
    /// Return the center of the related bounding box.
    Vec3d center() const { return this->bounding_box().center(); }
    // Returns the convex hull of this TriangleMesh
    TriangleMesh convex_hull_3d() const;
    // Slice this mesh at the provided Z levels and return the vector
    std::vector<ExPolygons> slice(const std::vector<double>& z);
    void reset_repair_stats();
    bool needed_repair() const;
    void require_shared_vertices();
    bool   has_shared_vertices() const { return ! this->its.vertices.empty(); }
    size_t facets_count() const { return this->stl.stats.number_of_facets; }
    bool   empty() const { return this->facets_count() == 0; }
    bool is_splittable() const;
    // Estimate of the memory occupied by this structure, important for keeping an eye on the Undo / Redo stack allocation.
    size_t memsize() const;
    // Release optional data from the mesh if the object is on the Undo / Redo stack only. Returns the amount of memory released.
    size_t release_optional();
    // Restore optional data possibly released by release_optional().
    void restore_optional();

    stl_file stl;
    indexed_triangle_set its;
    bool repaired;

private:
    std::deque<uint32_t> find_unvisited_neighbors(std::vector<unsigned char> &facet_visited) const;
};

// Create an index of faces belonging to each vertex. The returned vector can
// be indexed with vertex indices and contains a list of face indices for each
// vertex.
std::vector<std::vector<size_t>> create_vertex_faces_index(const indexed_triangle_set &its);

// Map from a face edge to a unique edge identifier or -1 if no neighbor exists.
// Two neighbor faces share a unique edge identifier even if they are flipped.
// Used for chaining slice lines into polygons.
std::vector<Vec3i> create_face_neighbors_index(const indexed_triangle_set &its);
std::vector<Vec3i> create_face_neighbors_index(const indexed_triangle_set &its, std::function<void()> throw_on_cancel_callback);

// Merge duplicate vertices, return number of vertices removed.
// This function will happily create non-manifolds if more than two faces share the same vertex position
// or more than two faces share the same edge position!
int its_merge_vertices(indexed_triangle_set &its, bool shrink_to_fit = true);

// Remove degenerate faces, return number of faces removed.
int its_remove_degenerate_faces(indexed_triangle_set &its, bool shrink_to_fit = true);

// Remove vertices, which none of the faces references. Return number of freed vertices.
int its_compactify_vertices(indexed_triangle_set &its, bool shrink_to_fit = true);

// Shrink the vectors of its.vertices and its.faces to a minimum size by reallocating the two vectors.
void its_shrink_to_fit(indexed_triangle_set &its);

TriangleMesh make_cube(double x, double y, double z);

// Generate a TriangleMesh of a cylinder
TriangleMesh make_cylinder(double r, double h, double fa=(2*PI/360));

TriangleMesh make_sphere(double rho, double fa=(2*PI/360));

}

// Serialization through the Cereal library
#include <cereal/access.hpp>
namespace cereal {
    template <class Archive> struct specialize<Archive, Slic3r::TriangleMesh, cereal::specialization::non_member_load_save> {};
    template<class Archive> void load(Archive &archive, Slic3r::TriangleMesh &mesh) {
        stl_file &stl = mesh.stl;
        stl.stats.type = inmemory;
        archive(stl.stats.number_of_facets, stl.stats.original_num_facets);
        stl_allocate(&stl);
        archive.loadBinary((char*)stl.facet_start.data(), stl.facet_start.size() * 50);
        stl_get_size(&stl);
        mesh.repair();
    }
    template<class Archive> void save(Archive &archive, const Slic3r::TriangleMesh &mesh) {
        const stl_file& stl = mesh.stl;
        archive(stl.stats.number_of_facets, stl.stats.original_num_facets);
        archive.saveBinary((char*)stl.facet_start.data(), stl.facet_start.size() * 50);
    }
}

#endif
