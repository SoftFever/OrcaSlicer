///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoMeasure_hpp_
#define slic3r_GLGizmoMeasure_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/Measure.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {

enum class ModelVolumeType : int;

namespace Measure { class Measuring; }


namespace GUI {

enum class SLAGizmoEventType : unsigned char;

class GLGizmoMeasure : public GLGizmoBase
{
    enum class EMode : unsigned char
    {
        FeatureSelection,
        PointSelection
    };

    struct SelectedFeatures
    {
        struct Item
        {
            bool is_center{ false };
            std::optional<Measure::SurfaceFeature> source;
            std::optional<Measure::SurfaceFeature> feature;

            bool operator == (const Item& other) const {
                return this->is_center == other.is_center && this->source == other.source && this->feature == other.feature;
            }

            bool operator != (const Item& other) const {
                return !operator == (other);
            }

            void reset() {
                is_center = false;
                source.reset();
                feature.reset();
            }
        };

        Item first;
        Item second;

        void reset() {
            first.reset();
            second.reset();
        }

        bool operator == (const SelectedFeatures & other) const {
            if (this->first != other.first) return false;
            return this->second == other.second;
        }

        bool operator != (const SelectedFeatures & other) const {
            return !operator == (other);
        }
    };

    struct VolumeCacheItem
    {
        const ModelObject* object{ nullptr };
        const ModelInstance* instance{ nullptr };
        const ModelVolume* volume{ nullptr };
        Transform3d world_trafo;

        bool operator == (const VolumeCacheItem& other) const {
            return this->object == other.object && this->instance == other.instance && this->volume == other.volume &&
                this->world_trafo.isApprox(other.world_trafo);
        }
    };

    std::vector<VolumeCacheItem> m_volumes_cache;

    EMode m_mode{ EMode::FeatureSelection };
    Measure::MeasurementResult m_measurement_result;

    std::unique_ptr<Measure::Measuring> m_measuring; // PIMPL

    PickingModel m_sphere;
    PickingModel m_cylinder;
    PickingModel m_circle;
    PickingModel m_plane;
    struct Dimensioning
    {
        GLModel line;
        GLModel triangle;
        GLModel arc;
    };
    Dimensioning m_dimensioning;

    // Uses a standalone raycaster and not the shared one because of the
    // difference in how the mesh is updated
    std::unique_ptr<MeshRaycaster> m_raycaster;

    std::vector<GLModel> m_plane_models_cache;
    std::map<int, std::shared_ptr<SceneRaycasterItem>> m_raycasters;
    // used to keep the raycasters for point/center spheres
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_selected_sphere_raycasters;
    std::optional<Measure::SurfaceFeature> m_curr_feature;
    std::optional<Vec3d> m_curr_point_on_feature_position;
    struct SceneRaycasterState
    {
        std::shared_ptr<SceneRaycasterItem> raycaster{ nullptr };
        bool state{true};

    };
    std::vector<SceneRaycasterState> m_scene_raycasters;

    // These hold information to decide whether recalculation is necessary:
    float m_last_inv_zoom{ 0.0f };
    std::optional<Measure::SurfaceFeature> m_last_circle;
    int m_last_plane_idx{ -1 };

    bool m_mouse_left_down{ false }; // for detection left_up of this gizmo

    Vec2d m_mouse_pos{ Vec2d::Zero() };

    KeyAutoRepeatFilter m_shift_kar_filter;

    SelectedFeatures m_selected_features;
    bool m_pending_scale{ false };
    bool m_editing_distance{ false };
    bool m_is_editing_distance_first_frame{ true };

    void update_if_needed();

    void disable_scene_raycasters();
    void restore_scene_raycasters_state();

    void render_dimensioning();

#if ENABLE_MEASURE_GIZMO_DEBUG
    void render_debug_dialog();
#endif // ENABLE_MEASURE_GIZMO_DEBUG

public:
    GLGizmoMeasure(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    /// <summary>
    /// Apply rotation on select plane
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

    void data_changed(bool is_serializing) override;

    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);

    bool wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return _u8L("Entering Measure gizmo"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Measure gizmo"); }
    std::string get_action_snapshot_name() const override { return _u8L("Measure gizmo editing"); }

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    void on_render() override;
    void on_set_state() override;

    virtual void on_render_input_window(float x, float y, float bottom_limit) override;
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;

    void remove_selected_sphere_raycaster(int id);
    void update_measurement_result();

    // Orca
    void show_tooltip_information(float caption_max, float x, float y);

private:
    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMeasure_hpp_
