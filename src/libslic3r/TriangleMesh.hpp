#ifndef slic3r_TriangleMesh_hpp_
#define slic3r_TriangleMesh_hpp_

#include "libslic3r.h"
#include <admesh/stl.h>
#include <functional>
#include <vector>
#include <boost/thread.hpp>
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
    TriangleMesh(const Pointf3s &points, const std::vector<Vec3crd> &facets);
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

enum FacetEdgeType { 
    // A general case, the cutting plane intersect a face at two different edges.
    feGeneral,
    // Two vertices are aligned with the cutting plane, the third vertex is below the cutting plane.
    feTop,
    // Two vertices are aligned with the cutting plane, the third vertex is above the cutting plane.
    feBottom,
    // All three vertices of a face are aligned with the cutting plane.
    feHorizontal
};

class IntersectionReference
{
public:
    IntersectionReference() : point_id(-1), edge_id(-1) {};
    IntersectionReference(int point_id, int edge_id) : point_id(point_id), edge_id(edge_id) {}
    // Where is this intersection point located? On mesh vertex or mesh edge?
    // Only one of the following will be set, the other will remain set to -1.
    // Index of the mesh vertex.
    int point_id;
    // Index of the mesh edge.
    int edge_id;
};

class IntersectionPoint : public Point, public IntersectionReference
{
public:
    IntersectionPoint() {};
    IntersectionPoint(int point_id, int edge_id, const Point &pt) : IntersectionReference(point_id, edge_id), Point(pt) {}
    IntersectionPoint(const IntersectionReference &ir, const Point &pt) : IntersectionReference(ir), Point(pt) {}
    // Inherits coord_t x, y
};

class IntersectionLine : public Line
{
public:
    IntersectionLine() : a_id(-1), b_id(-1), edge_a_id(-1), edge_b_id(-1), edge_type(feGeneral), flags(0) {}

    bool skip() const { return (this->flags & SKIP) != 0; }
    void set_skip() { this->flags |= SKIP; }

    bool is_seed_candidate() const { return (this->flags & NO_SEED) == 0 && ! this->skip(); }
    void set_no_seed(bool set) { if (set) this->flags |= NO_SEED; else this->flags &= ~NO_SEED; }
    
    // Inherits Point a, b
    // For each line end point, either {a,b}_id or {a,b}edge_a_id is set, the other is left to -1.
    // Vertex indices of the line end points.
    int             a_id;
    int             b_id;
    // Source mesh edges of the line end points.
    int             edge_a_id;
    int             edge_b_id;
    // feGeneral, feTop, feBottom, feHorizontal
    FacetEdgeType   edge_type;
    // Used by TriangleMeshSlicer::slice() to skip duplicate edges.
    enum {
        // Triangle edge added, because it has no neighbor.
        EDGE0_NO_NEIGHBOR   = 0x001,
        EDGE1_NO_NEIGHBOR   = 0x002,
        EDGE2_NO_NEIGHBOR   = 0x004,
        // Triangle edge added, because it makes a fold with another horizontal edge.
        EDGE0_FOLD          = 0x010,
        EDGE1_FOLD          = 0x020,
        EDGE2_FOLD          = 0x040,
        // The edge cannot be a seed of a greedy loop extraction (folds are not safe to become seeds).
        NO_SEED             = 0x100,
        SKIP                = 0x200,
    };
    uint32_t        flags;
};
typedef std::vector<IntersectionLine> IntersectionLines;
typedef std::vector<IntersectionLine*> IntersectionLinePtrs;

class TriangleMeshSlicer
{
public:
    typedef std::function<void()> throw_on_cancel_callback_type;
    TriangleMeshSlicer() : mesh(nullptr) {}
	TriangleMeshSlicer(const TriangleMesh* mesh) { this->init(mesh, [](){}); }
    void init(const TriangleMesh *mesh, throw_on_cancel_callback_type throw_on_cancel);
    void slice(const std::vector<float> &z, std::vector<Polygons>* layers, throw_on_cancel_callback_type throw_on_cancel) const;
    void slice(const std::vector<float> &z, const float closing_radius, std::vector<ExPolygons>* layers, throw_on_cancel_callback_type throw_on_cancel) const;
    enum FacetSliceType {
        NoSlice = 0,
        Slicing = 1,
        Cutting = 2
    };
    FacetSliceType slice_facet(float slice_z, const stl_facet &facet, const int facet_idx,
        const float min_z, const float max_z, IntersectionLine *line_out) const;
    void cut(float z, TriangleMesh* upper, TriangleMesh* lower) const;
    void set_up_direction(const Vec3f& up);
    
private:
    const TriangleMesh      *mesh;
    // Map from a facet to an edge index.
    std::vector<int>         facets_edges;
    // Scaled copy of this->mesh->stl.v_shared
    std::vector<stl_vertex>  v_scaled_shared;
    // Quaternion that will be used to rotate every facet before the slicing
    Eigen::Quaternion<float, Eigen::DontAlign> m_quaternion;
    // Whether or not the above quaterion should be used
    bool                     m_use_quaternion = false;

    void _slice_do(size_t facet_idx, std::vector<IntersectionLines>* lines, boost::mutex* lines_mutex, const std::vector<float> &z) const;
    void make_loops(std::vector<IntersectionLine> &lines, Polygons* loops) const;
    void make_expolygons(const Polygons &loops, const float closing_radius, ExPolygons* slices) const;
    void make_expolygons_simple(std::vector<IntersectionLine> &lines, ExPolygons* slices) const;
    void make_expolygons(std::vector<IntersectionLine> &lines, const float closing_radius, ExPolygons* slices) const;
};

inline void slice_mesh(
    const TriangleMesh &                              mesh,
    const std::vector<float> &                        z,
    std::vector<Polygons> &                           layers,
    TriangleMeshSlicer::throw_on_cancel_callback_type thr = nullptr)
{
    if (mesh.empty()) return;
    TriangleMeshSlicer slicer(&mesh);
    slicer.slice(z, &layers, thr);
}

inline void slice_mesh(
    const TriangleMesh &                              mesh,
    const std::vector<float> &                        z,
    std::vector<ExPolygons> &                         layers,
    float                                             closing_radius,
    TriangleMeshSlicer::throw_on_cancel_callback_type thr = nullptr)
{
    if (mesh.empty()) return;
    TriangleMeshSlicer slicer(&mesh);
    slicer.slice(z, closing_radius, &layers, thr);
}

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
