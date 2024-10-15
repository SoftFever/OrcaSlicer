#ifndef slic3r_GLGizmoMeshBoolean_hpp_
#define slic3r_GLGizmoMeshBoolean_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {

namespace GUI {

enum class MeshBooleanSelectingState {
    Undef,
    SelectSource,
    SelectTool,

};
enum class MeshBooleanOperation{
    Undef =-1,
    Union,
    Difference,
    Intersection,
};
struct VolumeInfo {
    ModelVolume* mv{ nullptr };
    int volume_idx{-1};
    Transform3d trafo;
    void reset() {
        mv = nullptr;
        volume_idx = -1;
        trafo = Transform3d::Identity();
    };
    template<class Archive>
    void serialize(Archive& ar) {
        ar(volume_idx, trafo);
    }
};
class GLGizmoMeshBoolean : public GLGizmoBase
{
public:
    GLGizmoMeshBoolean(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoMeshBoolean();

    void set_enable(bool enable) { m_enable = enable; }
    bool get_enable() const { return m_enable; }
    MeshBooleanSelectingState get_selecting_state() const { return m_selecting_state; }
    void set_src_volume(ModelVolume* mv);
    void set_tool_volume(ModelVolume* mv);

    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual std::string on_get_name_str() override { return "Mesh Boolean"; }
    virtual bool on_is_activable() const override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override {}
    virtual void on_set_state() override;
    virtual CommonGizmosDataID on_get_requirements() const override;
    virtual void on_render_input_window(float x, float y, float bottom_limit);
    virtual void render_input_window_warning(const std::string &text);
    void on_load(cereal::BinaryInputArchive &ar) override;
    void on_save(cereal::BinaryOutputArchive &ar) const override;

private:
    bool m_enable{ false };
    MeshBooleanOperation m_operation_mode;
    MeshBooleanSelectingState m_selecting_state;
    bool m_diff_delete_input = false;
    bool m_inter_delete_input = false;
    std::array<std::string,3> m_warning_texts;
    VolumeInfo m_src;
    VolumeInfo m_tool;

    void generate_new_volume(bool delete_input, const TriangleMesh& mesh_result);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMeshBoolean_hpp_