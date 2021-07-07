#ifndef slic3r_GLGizmoMmuSegmentation_hpp_
#define slic3r_GLGizmoMmuSegmentation_hpp_

#include "GLGizmoPainterBase.hpp"

namespace Slic3r::GUI {

class TriangleSelectorMmuGui : public TriangleSelectorGUI {
public:
    explicit TriangleSelectorMmuGui(const TriangleMesh& mesh, const std::vector<std::array<float, 4>> &colors, const std::array<float, 4> &default_volume_color)
        : TriangleSelectorGUI(mesh), m_colors(colors), m_default_volume_color(default_volume_color) {
        // Plus 1 is because the first position is allocated for non-painted triangles.
        m_iva_colors = std::vector<GLIndexedVertexArray>(colors.size() + 1);
    }
    ~TriangleSelectorMmuGui() override = default;

    // Render current selection. Transformation matrices are supposed
    // to be already set.
    void render(ImGuiWrapper* imgui) override;

private:
    const std::vector<std::array<float, 4>> &m_colors;
    std::vector<GLIndexedVertexArray>        m_iva_colors;
    const std::array<float, 4>               m_default_volume_color;
    GLIndexedVertexArray                     m_iva_seed_fill;
};

class GLGizmoMmuSegmentation : public GLGizmoPainterBase
{
public:
    GLGizmoMmuSegmentation(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
        : GLGizmoPainterBase(parent, icon_filename, sprite_id) {}
    ~GLGizmoMmuSegmentation() override = default;

    void render_painter_gizmo() const override;

    void set_painter_gizmo_data(const Selection& selection) override;

    // TriangleSelector::serialization/deserialization has a limit to store 19 different states.
    // EXTRUDER_LIMIT + 1 states are used to storing the painting because also uncolored triangles are stored.
    // When increasing EXTRUDER_LIMIT, it needs to ensure that TriangleSelector::serialization/deserialization
    // will be also extended to support additional states, requiring at least one state to remain free out of 19 states.
    static const constexpr size_t EXTRUDERS_LIMIT = 16;

protected:
    std::array<float, 4> get_cursor_sphere_left_button_color() const override;
    std::array<float, 4> get_cursor_sphere_right_button_color() const override;

    EnforcerBlockerType get_left_button_state_type() const override { return EnforcerBlockerType(m_first_selected_extruder_idx + 1); }
    EnforcerBlockerType get_right_button_state_type() const override { return EnforcerBlockerType(m_second_selected_extruder_idx + 1); }

    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

    bool on_is_selectable() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    size_t                            m_first_selected_extruder_idx  = 0;
    size_t                            m_second_selected_extruder_idx = 1;
    std::vector<std::string>          m_original_extruders_names;
    std::vector<std::array<float, 4>> m_original_extruders_colors;
    std::vector<std::array<float, 4>> m_modified_extruders_colors;
    std::vector<int>                  m_original_volumes_extruder_idxs;

private:
    bool on_init() override;

    void update_model_object() const override;
    void update_from_model_object() override;

    void on_opening() override;
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    void init_model_triangle_selectors();
    void init_extruders_data();

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
};

} // namespace Slic3r


#endif // slic3r_GLGizmoMmuSegmentation_hpp_
