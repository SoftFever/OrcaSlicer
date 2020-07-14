#ifndef libslic3r_TriangleSelector_hpp_
#define libslic3r_TriangleSelector_hpp_

#define PRUSASLICER_TRIANGLE_SELECTOR_DEBUG


#include "Point.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

enum class FacetSupportType : int8_t;



// Following class holds information about selected triangles. It also has power
// to recursively subdivide the triangles and make the selection finer.
class TriangleSelector {
public:
    void set_edge_limit(float edge_limit);

    // Create new object on a TriangleMesh. The referenced mesh must
    // stay valid, a ptr to it is saved and used.
    explicit TriangleSelector(const TriangleMesh& mesh);

    // Select all triangles fully inside the circle, subdivide where needed.
    void select_patch(const Vec3f& hit,    // point where to start
                      int facet_start,     // facet that point belongs to
                      const Vec3f& source, // camera position (mesh coords)
                      const Vec3f& dir,    // direction of the ray (mesh coords)
                      float radius_sqr,    // squared radius of the cursor
                      FacetSupportType new_state);   // enforcer or blocker?


    // Set facet of the mesh to a given state. Only works for original triangles.
    void set_facet(int facet_idx, FacetSupportType state);

    // Clear everything and make the tree empty.
    void reset();

    // Remove all unnecessary data.
    void garbage_collect();

    // Store the division trees in compact form (a long stream of
    // bits for each triangle of the original mesh).
    std::map<int, std::vector<bool>> serialize() const;

    // Load serialized data. Assumes that correct mesh is loaded.
    void deserialize(const std::map<int, std::vector<bool>> data);


protected:
    // Triangle and info about how it's split.
    class Triangle {
    public:
        // Use TriangleSelector::push_triangle to create a new triangle.
        // It increments/decrements reference counter on vertices.
        Triangle(int a, int b, int c)
            : verts_idxs{a, b, c},
              state{FacetSupportType(0)},
              number_of_splits{0},
              special_side_idx{0},
              old_number_of_splits{0}
        {}
        // Indices into m_vertices.
        std::array<int, 3> verts_idxs;

        // Is this triangle valid or marked to be removed?
        bool valid{true};

        // Children triangles.
        std::array<int, 4> children;

        // Set the division type.
        void set_division(int sides_to_split, int special_side_idx = -1);

        // Get/set current state.
        void set_state(FacetSupportType type) { assert(! is_split()); state = type; }
        FacetSupportType get_state() const { assert(! is_split()); return state; }

        // Get info on how it's split.
        bool is_split() const { return number_of_split_sides() != 0; }
        int number_of_split_sides() const { return number_of_splits; }
        int special_side() const  { assert(is_split()); return special_side_idx; }
        bool was_split_before() const { return old_number_of_splits != 0; }
        void forget_history() { old_number_of_splits = 0; }

    private:
        int number_of_splits;
        int special_side_idx;
        FacetSupportType state;

        // How many children were spawned during last split?
        // Is not reset on remerging the triangle.
        int old_number_of_splits;
    };

    struct Vertex {
        explicit Vertex(const stl_vertex& vert)
            : v{vert},
              ref_cnt{0}
        {}
        stl_vertex v;
        int ref_cnt;
    };

    // Lists of vertices and triangles, both original and new
    std::vector<Vertex> m_vertices;
    std::vector<Triangle> m_triangles;
    const TriangleMesh* m_mesh;

    // Number of invalid triangles (to trigger garbage collection).
    int m_invalid_triangles;

    // Limiting length of triangle side (squared).
    float m_edge_limit_sqr = 1.f;

    // Number of original vertices and triangles.
    int m_orig_size_vertices;
    int m_orig_size_indices;

    // Cache for cursor position, radius and direction.
    struct Cursor {
        Vec3f center;
        Vec3f source;
        Vec3f dir;
        float radius_sqr;
    };

    Cursor m_cursor;

    // Private functions:
    bool select_triangle(int facet_idx, FacetSupportType type,
                         bool recursive_call = false);
    bool is_point_inside_cursor(const Vec3f& point) const;
    int  vertices_inside(int facet_idx) const;
    bool faces_camera(int facet) const;
    void undivide_triangle(int facet_idx);
    void split_triangle(int facet_idx);
    void remove_useless_children(int facet_idx); // No hidden meaning. Triangles are meant.
    bool is_pointer_in_triangle(int facet_idx) const;
    bool is_edge_inside_cursor(int facet_idx) const;
    void push_triangle(int a, int b, int c);
    void perform_split(int facet_idx, FacetSupportType old_state);
};




} // namespace Slic3r

#endif // libslic3r_TriangleSelector_hpp_
