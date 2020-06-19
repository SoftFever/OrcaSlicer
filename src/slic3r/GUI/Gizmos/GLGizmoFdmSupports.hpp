#ifndef slic3r_GLGizmoFdmSupports_hpp_
#define slic3r_GLGizmoFdmSupports_hpp_

#include "GLGizmoBase.hpp"

#include "slic3r/GUI/3DScene.hpp"

#include "libslic3r/ObjectID.hpp"

#include <cereal/types/vector.hpp>


namespace Slic3r {

enum class FacetSupportType : int8_t;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;
class ClippingPlane;


// Following class holds information about selected triangles. It also has power
// to recursively subdivide the triangles and make the selection finer.
class TriangleSelector {
public:
    void set_edge_limit(float edge_limit) { m_edge_limit_sqr = std::pow(edge_limit, 2.f); }
    void render() const;
    // Create new object on a TriangleMesh. The referenced mesh must
    // stay valid, a ptr to it is saved and used.
    explicit TriangleSelector(const TriangleMesh& mesh);

    // Select all triangles inside the circle, subdivide where needed.
    void select_patch(const Vec3f& hit,    // point where to start
                      int facet_start,     // facet that point belongs to
                      const Vec3f& source, // camera position (mesh coords)
                      const Vec3f& dir,    // direction of the ray (mesh coords)
                      float radius_sqr,    // squared radius of the cursor
                      FacetSupportType new_state);   // enforcer or blocker?

    void unselect_all();

    // Remove all unnecessary data (such as vertices that are not needed
    // because the selection has been made larger.
    void garbage_collect();

private:
    // Triangle and info about how it's split.
    struct Triangle {
    public:
        Triangle(int a, int b, int c)
            : verts_idxs{stl_triangle_vertex_indices(a, b, c)},
              division_type{0}
        {}
        stl_triangle_vertex_indices verts_idxs;

        // Is this triangle valid or marked to remove?
        bool valid{true};

        // Index of parent triangle (-1: original)
        int parent{-1};

        // Children triangles (0 = no child)
        std::array<int, 4> children;

        // Set the division type.
        void set_division(int sides_to_split, int special_side_idx = -1);

        // Get/set current state.
        void set_state(FacetSupportType state);
        FacetSupportType get_state() const;

        // Get info on how it's split.
        bool is_split() const { return number_of_split_sides() != 0; }
        int number_of_split_sides() const { return division_type & 0b11; }
        int side_to_keep() const;
        int side_to_split() const;

    private:
        // Bitmask encoding which sides are split.
        int8_t division_type;
        // bits 0, 1 : decimal 0, 1, 2 or 3 (how many sides are split)
        // bits 2, 3 (non-leaf): decimal 0, 1 or 2 identifying the special edge
        //   (one that splits in one-edge split or one that stays in two-edge split).
        // bits 2, 3 (leaf): FacetSupportType value
    };

    // Lists of vertices and triangles, both original and new
    std::vector<stl_vertex> m_vertices;
    std::vector<Triangle> m_triangles;
    const TriangleMesh* m_mesh;

    float m_edge_limit_sqr = 1.f;

    // Number of original vertices and triangles.
    int m_orig_size_vertices;
    int m_orig_size_indices;

    // Limits for stopping the recursion.
    float m_max_edge_length;
    int m_max_recursion_depth;

    // Caches for cursor position, radius and direction.
    struct Cursor {
        Vec3f center;
        Vec3f source;
        Vec3f dir;
        float radius_sqr;
    };

    Cursor m_cursor;

    // Private functions:
    bool select_triangle(int facet_idx, FacetSupportType type,
                         bool cursor_inside = false);

    bool is_point_inside_cursor(const Vec3f& point) const;

    int vertices_inside(int facet_idx) const;

    bool faces_camera(int facet) const;

    void undivide_triangle(int facet_idx);

    bool split_triangle(int facet_idx);

    void remove_if_needless(int child_facet);
    bool is_pointer_in_triangle(int facet_idx) const;
};



class GLGizmoFdmSupports : public GLGizmoBase
{
private:
    ObjectID m_old_mo_id;
    size_t m_old_volumes_size = 0;

    GLUquadricObj* m_quadric;

    float m_cursor_radius = 2.f;
    static constexpr float CursorRadiusMin  = 0.f;
    static constexpr float CursorRadiusMax  = 8.f;
    static constexpr float CursorRadiusStep = 0.2f;

    // For each model-part volume, store a list of statuses of
    // individual facets (one of the enum values above).
    std::vector<std::vector<FacetSupportType>> m_selected_facets;

    GLIndexedVertexArray m_iva;

    void update_vertex_buffers(const TriangleMesh* mesh,
                               int mesh_id,
                               FacetSupportType type, // enforcers / blockers
                               const std::vector<size_t>* new_facets = nullptr); // nullptr -> regenerate all


public:
    GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoFdmSupports() override;
    void set_fdm_support_data(ModelObject* model_object, const Selection& selection);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);


private:
    bool on_init() override;
    void on_render() const override;
    void on_render_for_picking() const override {}

    void render_triangles(const Selection& selection) const;
    void render_cursor_circle() const;

    void update_model_object() const;
    void update_from_model_object();
    void activate_internal_undo_redo_stack(bool activate);

    void select_facets_by_angle(float threshold, bool overwrite, bool block);
    bool m_overwrite_selected = false;
    float m_angle_threshold_deg = 45.f;

    bool is_mesh_point_clipped(const Vec3d& point) const;

    float m_clipping_plane_distance = 0.f;
    std::unique_ptr<ClippingPlane> m_clipping_plane;
    bool m_setting_angle = false;
    bool m_internal_stack_active = false;
    bool m_schedule_update = false;
    
    std::unique_ptr<TriangleSelector> m_triangle_selector;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;

    enum class Button {
        None,
        Left,
        Right
    };

    Button m_button_down = Button::None;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

protected:
    void on_set_state() override;
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    bool on_is_selectable() const override;
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override;
    CommonGizmosDataID on_get_requirements() const override;
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoFdmSupports_hpp_
