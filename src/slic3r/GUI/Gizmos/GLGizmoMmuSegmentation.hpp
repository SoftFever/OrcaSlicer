#ifndef slic3r_GLGizmoMmuSegmentation_hpp_
#define slic3r_GLGizmoMmuSegmentation_hpp_

#include "GLGizmoPainterBase.hpp"

namespace Slic3r::GUI {

class TriangleSelectorMmuGui : public TriangleSelectorGUI {
public:
    explicit TriangleSelectorMmuGui(const TriangleMesh& mesh, const std::vector<std::array<uint8_t, 3>> &colors)
        : TriangleSelectorGUI(mesh), m_colors(colors) {
        m_iva_colors = std::vector<GLIndexedVertexArray>(colors.size());
    }

    // Render current selection. Transformation matrices are supposed
    // to be already set.
    void render(ImGuiWrapper* imgui) override;

private:
    const std::vector<std::array<uint8_t, 3>> &m_colors;
    std::vector<GLIndexedVertexArray> m_iva_colors;
};

class GLGizmoMmuSegmentation : public GLGizmoPainterBase
{
public:
    GLGizmoMmuSegmentation(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
        : GLGizmoPainterBase(parent, icon_filename, sprite_id) {}

    void render_painter_gizmo() const override;

    void set_painter_gizmo_data(const Selection& selection) override;

protected:
    std::array<float, 4> get_cursor_sphere_left_button_color() const override;
    std::array<float, 4> get_cursor_sphere_right_button_color() const override;

    EnforcerBlockerType get_left_button_state_type() const override { return EnforcerBlockerType(m_first_selected_extruder_idx); }
    EnforcerBlockerType get_right_button_state_type() const override { return EnforcerBlockerType(m_second_selected_extruder_idx); }

    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

    bool on_is_selectable() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    size_t                              m_first_selected_extruder_idx  = 0;
    size_t                              m_second_selected_extruder_idx = 1;
    std::vector<std::string>            m_original_extruders_names;
    std::vector<std::array<uint8_t, 3>> m_original_extruders_colors;
    std::vector<std::array<uint8_t, 3>> m_modified_extruders_colors;

private:
    bool on_init() override;

    void update_model_object() const override;
    void update_from_model_object() override;

    void on_opening() override {}
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
