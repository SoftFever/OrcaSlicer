#include "slic3r/GUI/ImGuiWrapper.hpp"
#include <imgui/imgui_internal.h>

#include "GizmoObjectManipulation.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
//#include "I18N.hpp"
#include "GLGizmosManager.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Geometry.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include <boost/algorithm/string.hpp>

#define MAX_NUM 9999.99
#define MAX_SIZE std::string_view{"9999.99"}

namespace Slic3r
{
namespace GUI
{

const double GizmoObjectManipulation::in_to_mm = 25.4;
const double GizmoObjectManipulation::mm_to_in = 0.0393700787;
const double GizmoObjectManipulation::oz_to_g = 28.34952;
const double GizmoObjectManipulation::g_to_oz = 0.035274;

// Helper function to be used by drop to bed button. Returns lowest point of this
// volume in world coordinate system.
static double get_volume_min_z(const GLVolume* volume)
{
    const Transform3f& world_matrix = volume->world_matrix().cast<float>();

    // need to get the ModelVolume pointer
    const ModelObject* mo = wxGetApp().model().objects[volume->composite_id.object_id];
    const ModelVolume* mv = mo->volumes[volume->composite_id.volume_id];
    const TriangleMesh& hull = mv->get_convex_hull();

    float min_z = std::numeric_limits<float>::max();
    for (const stl_vertex& vert : hull.its.vertices) {
        min_z = std::min(min_z, Vec3f::UnitZ().dot(world_matrix * vert));
    }
    return min_z;
}

GizmoObjectManipulation::GizmoObjectManipulation(GLCanvas3D& glcanvas)
    : m_glcanvas(glcanvas)
{
    m_imperial_units = wxGetApp().app_config->get("use_inches") == "1";
    m_new_unit_string = m_imperial_units ? L("in") : L("mm");
}

void GizmoObjectManipulation::UpdateAndShow(const bool show)
{
	if (show) {
        this->set_dirty();
		this->update_if_dirty();
	}
}

void GizmoObjectManipulation::update_ui_from_settings()
{
    if (m_imperial_units != (wxGetApp().app_config->get("use_inches") == "1")) {
        m_imperial_units = wxGetApp().app_config->get("use_inches") == "1";

        m_new_unit_string = m_imperial_units ? L("in") : L("mm");

        update_buffered_value();
    }
}

void GizmoObjectManipulation::update_settings_value(const Selection& selection)
{
	m_new_move_label_string   = L("Position");
    m_new_rotate_label_string = L("Rotation");
    m_new_scale_label_string  = L("Scale ratios");

    m_coordinates_type = ECoordinatesType::World;

    ObjectList* obj_list = wxGetApp().obj_list();
    if (selection.is_single_full_instance()) {
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_first_volume();
        m_new_position = volume->get_instance_offset();

        if (is_world_coordinates()) {
			m_new_rotate_label_string = L("Rotate");
            m_new_rotation = volume->get_instance_rotation() * (180. / M_PI);
			m_new_size     = selection.get_scaled_instance_bounding_box().size();
			m_new_scale    = m_new_size.cwiseProduct(selection.get_unscaled_instance_bounding_box().size().cwiseInverse()) * 100.;
		} 
        else {
			m_new_rotation = volume->get_instance_rotation() * (180. / M_PI);
			m_new_size     = volume->get_instance_transformation().get_scaling_factor().cwiseProduct(wxGetApp().model().objects[volume->object_idx()]->raw_mesh_bounding_box().size());
			m_new_scale    = volume->get_instance_scaling_factor() * 100.;
		}

        m_new_enabled  = true;
        // BBS: change "Instance Operations" to "Object Operations"
        m_new_title_string = L("Object Operations");
    }
    else if (selection.is_single_full_object() && obj_list->is_selected(itObject)) {
        const BoundingBoxf3& box = selection.get_bounding_box();
        m_new_position = box.center();
        m_new_rotation = Vec3d::Zero();
        m_new_scale    = Vec3d(100., 100., 100.);
        m_new_size     = box.size();
        m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_enabled  = true;
        m_new_title_string = L("Object Operations");
    }
    else if (selection.is_single_modifier() || selection.is_single_volume()) {
        // the selection contains a single volume
        const GLVolume* volume = selection.get_first_volume();
        m_new_position = volume->get_volume_offset();
        m_new_rotation = volume->get_volume_rotation() * (180. / M_PI);
        m_new_scale    = volume->get_volume_scaling_factor() * 100.;
        m_new_size     = volume->get_instance_transformation().get_scaling_factor().cwiseProduct(volume->get_volume_transformation().get_scaling_factor().cwiseProduct(volume->bounding_box().size()));
        m_new_enabled = true;
        m_new_title_string = L("Volume Operations");
    }
    else if (obj_list->multiple_selection() || obj_list->is_selected(itInstanceRoot)) {
        reset_settings_value();
		m_new_move_label_string   = L("Translate");
		m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_size = selection.get_bounding_box().size();
        m_new_enabled  = true;
        m_new_title_string = L("Group Operations");
    }
	else {
        // No selection, reset the cache.
//		assert(selection.is_empty());
		reset_settings_value();
	}
}

void GizmoObjectManipulation::update_buffered_value()
{
    if (this->m_imperial_units)
        m_buffered_position = this->m_new_position * this->mm_to_in;
    else
        m_buffered_position = this->m_new_position;

    m_buffered_rotation = this->m_new_rotation;

    m_buffered_scale = this->m_new_scale;

    if (this->m_imperial_units)
        m_buffered_size = this->m_new_size * this->mm_to_in;
    else
        m_buffered_size = this->m_new_size;
}

void GizmoObjectManipulation::update_if_dirty()
{
    if (! m_dirty)
        return;

    const Selection &selection = m_glcanvas.get_selection();
    this->update_settings_value(selection);
    this->update_buffered_value();

    auto update_label = [](wxString &label_cache, const std::string &new_label) {
        wxString new_label_localized = _(new_label) + ":";
        if (label_cache != new_label_localized) {
            label_cache = new_label_localized;
        }
    };
    update_label(m_cache.move_label_string,   m_new_move_label_string);
    update_label(m_cache.rotate_label_string, m_new_rotate_label_string);
    update_label(m_cache.scale_label_string,  m_new_scale_label_string);

    enum ManipulationEditorKey
    {
        mePosition = 0,
        meRotation,
        meScale,
        meSize
    };

    for (int i = 0; i < 3; ++ i) {
        auto update = [this, i](Vec3d &cached, Vec3d &cached_rounded,  const Vec3d &new_value) {
			//wxString new_text = double_to_string(new_value(i), 2);
			double new_rounded = round(new_value(i)*100)/100.0;
			//new_text.ToDouble(&new_rounded);
			if (std::abs(cached_rounded(i) - new_rounded) > EPSILON) {
				cached_rounded(i) = new_rounded;
                //const int id = key_id*3+i;
                //if (m_imperial_units && (key_id == mePosition || key_id == meSize))
                //    new_text = double_to_string(new_value(i)*mm_to_in, 2);
                //if (id >= 0) m_editors[id]->set_value(new_text);
            }
			cached(i) = new_value(i);
		};
        update(m_cache.position, m_cache.position_rounded,  m_new_position);
        update(m_cache.scale,    m_cache.scale_rounded,     m_new_scale);
        update(m_cache.size,     m_cache.size_rounded,      m_new_size);
        update(m_cache.rotation, m_cache.rotation_rounded,  m_new_rotation);
    }

    update_reset_buttons_visibility();
    //update_mirror_buttons_visibility();

    m_dirty = false;
}

void GizmoObjectManipulation::update_reset_buttons_visibility()
{
    const Selection& selection = m_glcanvas.get_selection();

    if (selection.is_single_full_instance() || selection.is_single_modifier() || selection.is_single_volume()) {
        const GLVolume* volume = selection.get_first_volume();
        Vec3d rotation;
        Vec3d scale;
        double min_z = 0.;

        if (selection.is_single_full_instance()) {
            rotation = volume->get_instance_rotation();
            scale = volume->get_instance_scaling_factor();
        }
        else {
            rotation = volume->get_volume_rotation();
            scale = volume->get_volume_scaling_factor();
            min_z = get_volume_min_z(volume);
        }
        m_show_clear_rotation = !rotation.isApprox(Vec3d::Zero());
        m_show_clear_scale = !scale.isApprox(Vec3d::Ones(), EPSILON);
        m_show_drop_to_bed = (std::abs(min_z) > EPSILON);
    }
}


void GizmoObjectManipulation::reset_settings_value()
{
    m_new_position = Vec3d::Zero();
    m_new_rotation = Vec3d::Zero();
    m_new_scale = Vec3d::Ones() * 100.;
    m_new_size = Vec3d::Zero();
    m_new_enabled = false;
    // no need to set the dirty flag here as this method is called from update_settings_value(),
    // which is called from update_if_dirty(), which resets the dirty flag anyways.
//    m_dirty = true;
}

void GizmoObjectManipulation::change_position_value(int axis, double value)
{
    if (std::abs(m_cache.position_rounded(axis) - value) < EPSILON)
        return;

    Vec3d position = m_cache.position;
    position(axis) = value;

    Selection& selection = m_glcanvas.get_selection();
    selection.setup_cache();
    TransformationType trafo_type;
    trafo_type.set_relative();
    if (selection.requires_local_axes()) {
        trafo_type.set_instance();
    }
    selection.translate(position - m_cache.position, trafo_type);
    m_glcanvas.do_move(L("Set Position"));

    m_cache.position = position;
	m_cache.position_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void GizmoObjectManipulation::change_rotation_value(int axis, double value)
{
    if (std::abs(m_cache.rotation_rounded(axis) - value) < EPSILON)
        return;

    Vec3d rotation = m_cache.rotation;
    rotation(axis) = value;

    Selection& selection = m_glcanvas.get_selection();

    TransformationType transformation_type;
    transformation_type.set_relative();
    if (selection.is_single_full_instance())
        transformation_type.set_independent();

    if (is_local_coordinates())
        transformation_type.set_local();

    if (is_instance_coordinates())
        transformation_type.set_instance();

    selection.setup_cache();
	selection.rotate(
		(M_PI / 180.0) * (transformation_type.absolute() ? rotation : rotation - m_cache.rotation), 
		transformation_type);
    m_glcanvas.do_rotate(L("Set Orientation"));

    m_cache.rotation = rotation;
	m_cache.rotation_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void GizmoObjectManipulation::change_scale_value(int axis, double value)
{
    if (std::abs(m_cache.scale_rounded(axis) - value) < EPSILON)
        return;

    Vec3d scale = m_cache.scale;
    if (scale[axis] != 0 && std::abs(m_cache.size[axis] * value / scale[axis]) > MAX_NUM) {
        scale[axis] *= MAX_NUM / m_cache.size[axis];
    }
    else {
        scale(axis) = value;
    }

    this->do_scale(axis, scale);

    m_cache.scale = scale;
	m_cache.scale_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}


void GizmoObjectManipulation::change_size_value(int axis, double value)
{
    if (std::abs(m_cache.size_rounded(axis) - value) < EPSILON)
        return;

    Vec3d size = m_cache.size;
    size(axis) = value;

    const Selection& selection = m_glcanvas.get_selection();

    Vec3d ref_size = m_cache.size;
    if (selection.is_single_volume() || selection.is_single_modifier()) {
        Vec3d instance_scale = wxGetApp().model().objects[selection.get_first_volume()->object_idx()]->instances[0]->get_transformation().get_scaling_factor();
        ref_size = selection.get_first_volume()->bounding_box().size();
        ref_size = Vec3d(instance_scale[0] * ref_size[0], instance_scale[1] * ref_size[1], instance_scale[2] * ref_size[2]);
    }
    else if (selection.is_single_full_instance())
        ref_size = is_world_coordinates() ? 
            selection.get_unscaled_instance_bounding_box().size() :
            wxGetApp().model().objects[selection.get_first_volume()->object_idx()]->raw_mesh_bounding_box().size();

    this->do_scale(axis, 100. * Vec3d(size(0) / ref_size(0), size(1) / ref_size(1), size(2) / ref_size(2)));

    m_cache.size = size;
	m_cache.size_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}

void GizmoObjectManipulation::do_scale(int axis, const Vec3d &scale) const
{
    Selection& selection = m_glcanvas.get_selection();
    Vec3d scaling_factor = scale;

    TransformationType transformation_type(TransformationType::World_Relative_Joint);
    if (selection.is_single_full_instance()) {
        transformation_type.set_absolute();
        if (! is_world_coordinates())
            transformation_type.set_local();
    }

    // BBS: when select multiple objects, uniform scale can be deselected
    if (m_uniform_scale/* || selection.requires_uniform_scale()*/)
        scaling_factor = scale(axis) * Vec3d::Ones();

    selection.setup_cache();
    selection.scale_legacy(scaling_factor * 0.01, transformation_type);
    m_glcanvas.do_scale(L("Set Scale"));
}

void GizmoObjectManipulation::on_change(const std::string& opt_key, int axis, double new_value)
{
    if (!m_cache.is_valid())
        return;

    if (m_imperial_units && (opt_key == "position" || opt_key == "size"))
        new_value *= in_to_mm;

    if (opt_key == "position")
        change_position_value(axis, new_value);
    else if (opt_key == "rotation")
        change_rotation_value(axis, new_value);
    else if (opt_key == "scale")
        change_scale_value(axis, new_value);
    else if (opt_key == "size")
        change_size_value(axis, new_value);
}

void GizmoObjectManipulation::set_uniform_scaling(const bool new_value)
{ 
    const Selection &selection = m_glcanvas.get_selection();
    if (selection.is_single_full_instance() && is_world_coordinates() && !new_value) {
        // Verify whether the instance rotation is multiples of 90 degrees, so that the scaling in world coordinates is possible.
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_first_volume();
        // Is the angle close to a multiple of 90 degrees?

		if (! Geometry::is_rotation_ninety_degrees(volume->get_instance_rotation())) {
            // Cannot apply scaling in the world coordinate system.
            // BBS: remove tilt prompt dialog

            // Bake the rotation into the meshes of the object.
            wxGetApp().model().objects[volume->composite_id.object_id]->bake_xy_rotation_into_meshes(volume->composite_id.instance_id);
            // Update the 3D scene, selections etc.
            wxGetApp().plater()->update();
            // Recalculate cached values at this panel, refresh the screen.
            this->UpdateAndShow(true);
        }
    }
    m_uniform_scale = new_value;
}

void GizmoObjectManipulation::set_coordinates_type(ECoordinatesType type)
{
    if (wxGetApp().get_mode() == comSimple)
        type = ECoordinatesType::World;

    if (m_coordinates_type == type)
        return;

    m_coordinates_type = type;
    this->UpdateAndShow(true);
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    canvas->get_gizmos_manager().update_data();
    canvas->set_as_dirty();
    canvas->request_extra_frame();
}

ECoordinatesType GizmoObjectManipulation::get_coordinates_type() const
{
    return m_coordinates_type;
}

void GizmoObjectManipulation::reset_position_value()
{
    Selection& selection = m_glcanvas.get_selection();

    if (selection.is_single_volume() || selection.is_single_modifier()) {
        GLVolume* volume = const_cast<GLVolume*>(selection.get_first_volume());
        volume->set_volume_offset(Vec3d::Zero());
    }
    else if (selection.is_single_full_instance()) {
        for (unsigned int idx : selection.get_volume_idxs()) {
            GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
            volume->set_instance_offset(Vec3d::Zero());
        }
    }
    else
        return;

    // Copy position values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
    m_glcanvas.do_move(L("Reset Position"));

    UpdateAndShow(true);
}

void GizmoObjectManipulation::reset_rotation_value()
{
    Selection& selection = m_glcanvas.get_selection();

    if (selection.is_single_volume() || selection.is_single_modifier()) {
        GLVolume* volume = const_cast<GLVolume*>(selection.get_first_volume());
        volume->set_volume_rotation(Vec3d::Zero());
    }
    else if (selection.is_single_full_instance()) {
        for (unsigned int idx : selection.get_volume_idxs()) {
            GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
            volume->set_instance_rotation(Vec3d::Zero());
        }
    }
    else
        return;

    // Update rotation at the GLVolumes.
    selection.synchronize_unselected_instances(Selection::SyncRotationType::GENERAL);
    selection.synchronize_unselected_volumes();
    // Copy rotation values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
    m_glcanvas.do_rotate(L("Reset Rotation"));

    UpdateAndShow(true);
}

void GizmoObjectManipulation::reset_scale_value()
{
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Reset scale");

    change_scale_value(0, 100.);
    change_scale_value(1, 100.);
    change_scale_value(2, 100.);
}

static const char* label_values[2][3] = {
{ "##position_x", "##position_y", "##position_z"},
{ "##rotation_x", "##rotation_y", "##rotation_z"}
};

static const char* label_scale_values[2][3] = {
{ "##scale_x", "##scale_y", "##scale_z"},
{ "##size_x", "##size_y", "##size_z"}
};

bool GizmoObjectManipulation::reset_button(ImGuiWrapper *imgui_wrapper, float caption_max, float unit_size, float space_size, float end_text_size)
{
    bool        pressed   = false;
    ImTextureID normal_id = m_glcanvas.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_RESET);
    ImTextureID hover_id  = m_glcanvas.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_RESET_HOVER);

    float font_size = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size, font_size);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    pressed = ImGui::ImageButton3(normal_id, hover_id, button_size);

    ImGui::PopStyleVar(1);
    return pressed;
}

 float GizmoObjectManipulation::max_unit_size(int number, Vec3d &vec1, Vec3d &vec2,std::string str)
 {
     if (number <= 1) return -1;
     Vec3d vec[2] = {vec1, vec2};
     float nuit_max[4] = {0};
     float vec_max = 0, unit_size = 0;

     for (int i = 0; i < number; i++)
     {
         char buf[3][64] = {0};
         float buf_size[3] = {0};
         for (int j = 0; j < 3; j++) {
             ImGui::DataTypeFormatString(buf[j], IM_ARRAYSIZE(buf[j]), ImGuiDataType_Double, (void *) &vec[i][j], "%.2f");
             buf_size[j]  = ImGui::CalcTextSize(buf[j]).x;
             vec_max = std::max(buf_size[j], vec_max);
             nuit_max[i]  = vec_max;
         }
         unit_size = std::max(nuit_max[i], unit_size);
     }

     return unit_size + 8.0;
 }

void GizmoObjectManipulation::do_render_move_window(ImGuiWrapper *imgui_wrapper, std::string window_name, float x, float y, float bottom_limit)
{
    // BBS: GUI refactor: move gizmo to the right
    if (abs(last_move_input_window_width) > 0.01f) {
        if (x + last_move_input_window_width > m_glcanvas.get_canvas_size().get_width()) {
            if (last_move_input_window_width > m_glcanvas.get_canvas_size().get_width())
                x = 0;
            else
                x = m_glcanvas.get_canvas_size().get_width() - last_move_input_window_width;
        }
    }
#if BBS_TOOLBAR_ON_TOP
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 0.f, 0.0f);
#else
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    // BBS
    ImGuiWrapper::push_toolbar_style(m_glcanvas.get_scale());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0, 6.0));

    std::string name = this->m_new_title_string + "##" + window_name;
    imgui_wrapper->begin(_L(name), ImGuiWrapper::TOOLBAR_WINDOW_FLAGS);

    auto update = [this](unsigned int active_id, std::string opt_key, Vec3d original_value, Vec3d new_value) -> int {
        for (int i = 0; i < 3; i++) {
            if (original_value[i] != new_value[i]) {
                if (active_id != m_last_active_item) {
                    on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };

    float space_size    = imgui_wrapper->get_style_scaling() * 8;
    float position_size = imgui_wrapper->calc_text_size(_L("Position")).x + space_size;
    auto position_title = _L("World coordinates");
    Selection& selection = m_glcanvas.get_selection();
    if(selection.is_single_modifier() || selection.is_single_volume())
        position_title = _L("Object coordinates");

    float World_size    = imgui_wrapper->calc_text_size(position_title).x + space_size;
    float caption_max   = std::max(position_size, World_size) + 2 * space_size;
    float end_text_size = imgui_wrapper->calc_text_size(this->m_new_unit_string).x;

    // position
    Vec3d original_position;
    if (this->m_imperial_units)
        original_position = this->m_new_position * this->mm_to_in;
    else
        original_position = this->m_new_position;
    Vec3d display_position = m_buffered_position;

    // Rotation
    Vec3d rotation   = this->m_buffered_rotation;
    float unit_size = imgui_wrapper->calc_text_size(MAX_SIZE).x + space_size;
    int   index      = 1;
    int   index_unit = 1;

    ImGui::AlignTextToFramePadding();
    unsigned int current_active_id = ImGui::GetActiveID();
    ImGui::PushItemWidth(caption_max);
    imgui_wrapper->text(position_title);
    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("X");
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Y");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Z");

    index      = 1;
    index_unit = 1;
    ImGui::AlignTextToFramePadding();
    imgui_wrapper->text(_L("Position"));
    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_values[0][0], &display_position[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_values[0][1], &display_position[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_values[0][2], &display_position[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui_wrapper->text(this->m_new_unit_string);

    for (int i = 0;i<display_position.size();i++)
    {
        if (display_position[i] > MAX_NUM)display_position[i] = MAX_NUM;
        if (display_position[i] < -MAX_NUM)display_position[i] = -MAX_NUM;
    }

    m_buffered_position = display_position;
    update(current_active_id, "position", original_position, m_buffered_position);
    // the init position values are not zero, won't add reset button

    // send focus to m_glcanvas
    bool focued_on_text = false;
    for (int j = 0; j < 3; j++) {
        unsigned int id = ImGui::GetID(label_values[0][j]);
        if (current_active_id == id) {
            m_glcanvas.handle_sidebar_focus_event(label_values[0][j] + 2, true);
            focued_on_text = true;
            break;
        }
    }
    if (!focued_on_text) m_glcanvas.handle_sidebar_focus_event("", false);

    m_last_active_item = current_active_id;
    last_move_input_window_width = ImGui::GetWindowWidth();
    imgui_wrapper->end();
    ImGui::PopStyleVar(1);
    ImGuiWrapper::pop_toolbar_style();
}

void GizmoObjectManipulation::do_render_rotate_window(ImGuiWrapper *imgui_wrapper, std::string window_name, float x, float y, float bottom_limit)
{
    // BBS: GUI refactor: move gizmo to the right
    if (abs(last_rotate_input_window_width) > 0.01f) {
        if (x + last_rotate_input_window_width > m_glcanvas.get_canvas_size().get_width()) {
            if (last_rotate_input_window_width > m_glcanvas.get_canvas_size().get_width())
                x = 0;
            else
                x = m_glcanvas.get_canvas_size().get_width() - last_rotate_input_window_width;
        }
    }
#if BBS_TOOLBAR_ON_TOP
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 0.f, 0.0f);
#else
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    // BBS
    ImGuiWrapper::push_toolbar_style(m_glcanvas.get_scale());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0, 6.0));

    std::string name = this->m_new_title_string + "##" + window_name;
    imgui_wrapper->begin(_L(name), ImGuiWrapper::TOOLBAR_WINDOW_FLAGS);

    auto update = [this](unsigned int active_id, std::string opt_key, Vec3d original_value, Vec3d new_value) -> int {
        for (int i = 0; i < 3; i++) {
            if (original_value[i] != new_value[i]) {
                if (active_id != m_last_active_item) {
                    on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };

    float space_size    = imgui_wrapper->get_style_scaling() * 8;
    float position_size = imgui_wrapper->calc_text_size(_L("Rotation")).x + space_size;
    float World_size    = imgui_wrapper->calc_text_size(_L("World coordinates")).x + space_size;
    float caption_max   = std::max(position_size, World_size) + 2 * space_size;
    float end_text_size = imgui_wrapper->calc_text_size(this->m_new_unit_string).x;

    // position
    Vec3d original_position;
    if (this->m_imperial_units)
        original_position = this->m_new_position * this->mm_to_in;
    else
        original_position = this->m_new_position;
    Vec3d display_position = m_buffered_position;
    // Rotation
    Vec3d rotation   = this->m_buffered_rotation;

    float unit_size = imgui_wrapper->calc_text_size(MAX_SIZE).x + space_size;
    int   index      = 1;
    int   index_unit = 1;

    ImGui::AlignTextToFramePadding();
    unsigned int current_active_id = ImGui::GetActiveID();
    ImGui::PushItemWidth(caption_max);
    imgui_wrapper->text(_L("World coordinates"));
    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("X");
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Y");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Z");

    index      = 1;
    index_unit = 1;

    // ImGui::PushItemWidth(unit_size * 2);
    ImGui::AlignTextToFramePadding();
    imgui_wrapper->text(_L("Rotation"));
    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_values[1][0], &rotation[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_values[1][1], &rotation[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_values[1][2], &rotation[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui_wrapper->text(_L("°"));
    m_buffered_rotation = rotation;
    update(current_active_id, "rotation", this->m_new_rotation, m_buffered_rotation);

    if (m_show_clear_rotation) {
        ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size + end_text_size);
        if (reset_button(imgui_wrapper, caption_max, unit_size, space_size, end_text_size)) { reset_rotation_value(); }
    } else {
        ImGui::SameLine(caption_max + 3 * unit_size + 5 * space_size + end_text_size);
        ImGui::InvisibleButton("", ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
    }

    // send focus to m_glcanvas
    bool focued_on_text = false;
    for (int j = 0; j < 3; j++) {
        unsigned int id = ImGui::GetID(label_values[1][j]);
        if (current_active_id == id) {
            m_glcanvas.handle_sidebar_focus_event(label_values[1][j] + 2, true);
            focued_on_text = true;
            break;
        }
    }
    if (!focued_on_text) m_glcanvas.handle_sidebar_focus_event("", false);

    m_last_active_item = current_active_id;
    last_rotate_input_window_width = ImGui::GetWindowWidth();
    imgui_wrapper->end();

    // BBS
    ImGui::PopStyleVar(1);
    ImGuiWrapper::pop_toolbar_style();
}

void GizmoObjectManipulation::do_render_scale_input_window(ImGuiWrapper* imgui_wrapper, std::string window_name, float x, float y, float bottom_limit)
{
    //BBS: GUI refactor: move gizmo to the right
    if (abs(last_scale_input_window_width) > 0.01f) {
        if (x + last_scale_input_window_width > m_glcanvas.get_canvas_size().get_width()) {
            if (last_scale_input_window_width > m_glcanvas.get_canvas_size().get_width())
                x = 0;
            else
                x = m_glcanvas.get_canvas_size().get_width() - last_scale_input_window_width;
        }
    }
#if BBS_TOOLBAR_ON_TOP
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 0.f, 0.0f);
#else
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    //BBS
    ImGuiWrapper::push_toolbar_style(m_glcanvas.get_scale());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0, 6.0));

    std::string name = this->m_new_title_string + "##" + window_name;
    imgui_wrapper->begin(_L(name), ImGuiWrapper::TOOLBAR_WINDOW_FLAGS);

    auto update = [this](unsigned int active_id, std::string opt_key, Vec3d original_value, Vec3d new_value)->int {
        for (int i = 0; i < 3; i++)
        {
            if (original_value[i] != new_value[i])
            {
                if (active_id != m_last_active_item)
                {
                    on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };

    float space_size = imgui_wrapper->get_style_scaling() * 8;
    float scale_size = imgui_wrapper->calc_text_size(_L("Scale")).x + space_size;
    float size_len = imgui_wrapper->calc_text_size(_L("Size")).x + space_size;
    float caption_max = std::max(scale_size, size_len) + 2 * space_size;
    float end_text_size = imgui_wrapper->calc_text_size(this->m_new_unit_string).x;
    ImGui::AlignTextToFramePadding();
    unsigned int current_active_id = ImGui::GetActiveID();

    Vec3d scale = m_buffered_scale;
    Vec3d display_size = m_buffered_size;

    Vec3d display_position = m_buffered_position;

    float unit_size = imgui_wrapper->calc_text_size(MAX_SIZE).x + space_size;
    bool imperial_units = this->m_imperial_units;

    int index      = 2;
    int index_unit = 1;

    ImGui::PushItemWidth(caption_max);
    ImGui::Dummy(ImVec2(caption_max, -1));
    //imgui_wrapper->text(_L(" "));
    //ImGui::PushItemWidth(unit_size * 1.5);
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("X");
    ImGui::SameLine(caption_max + unit_size + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Y");
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Z");

    index      = 2;
    index_unit = 1;

    //ImGui::PushItemWidth(unit_size * 2);
    ImGui::AlignTextToFramePadding();
    imgui_wrapper->text(_L("Scale"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[0][0], &scale[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[0][1], &scale[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) *unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[0][2], &scale[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) *unit_size + (++index) * space_size);
    imgui_wrapper->text(_L("%"));
    m_buffered_scale = scale;

    if (m_show_clear_scale) {
        ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size + end_text_size);
        if (reset_button(imgui_wrapper, caption_max, unit_size, space_size, end_text_size))
            reset_scale_value();
    } else {
        ImGui::SameLine(caption_max + 3 * unit_size + 5 * space_size + end_text_size);
        ImGui::InvisibleButton("", ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
    }

    //Size
    Vec3d original_size;
    if (this->m_imperial_units)
        original_size = this->m_new_size * this->mm_to_in;
    else
        original_size = this->m_new_size;

    index              = 2;
    index_unit         = 1;
    //ImGui::PushItemWidth(unit_size * 2);
    ImGui::AlignTextToFramePadding();
    imgui_wrapper->text(_L("Size"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[1][0], &display_size[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + index * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[1][1], &display_size[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) *unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble(label_scale_values[1][2], &display_size[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + (++index_unit) *unit_size + (++index) * space_size);
    imgui_wrapper->text(this->m_new_unit_string);

    for (int i = 0;i<display_size.size();i++)
    {
        if (std::abs(display_size[i]) > MAX_NUM) display_size[i] = MAX_NUM;
    }
    m_buffered_size = display_size;
    int size_sel = update(current_active_id, "size", original_size, m_buffered_size);
    ImGui::PopStyleVar(1);

    ImGui::Separator();

    bool uniform_scale = this->m_uniform_scale;

    // BBS: when select multiple objects, uniform scale can be deselected
    //const Selection &selection = m_glcanvas.get_selection();
    //bool uniform_scale_only    = selection.is_multiple_full_object() || selection.is_multiple_full_instance() || selection.is_mixed() || selection.is_multiple_volume() ||
    //                          selection.is_multiple_modifier();

    //if (uniform_scale_only) {
    //    imgui_wrapper->disabled_begin(true);
    //    imgui_wrapper->bbl_checkbox(_L("uniform scale"), uniform_scale_only);
    //    imgui_wrapper->disabled_end();
    //} else {
        imgui_wrapper->bbl_checkbox(_L("uniform scale"), uniform_scale);
    //}
    if (uniform_scale != this->m_uniform_scale) { this->set_uniform_scaling(uniform_scale); }

     // for (int index = 0; index < 3; index++)
    //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",before_index="<<index <<boost::format(",scale %1%, buffered %2%, original_id %3%, new_id %4%\n") %
    //    this->m_new_scale[index] % m_buffered_scale[index] % m_last_active_item % current_active_id;
    int scale_sel = update(current_active_id, "scale", this->m_new_scale, m_buffered_scale);
    if ((scale_sel >= 0)) {
        // for (int index = 0; index < 3; index++)
        //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",after_index="<<index <<boost::format(",scale %1%, buffered %2%, original_id %3%, new_id %4%\n") %
        //    this->m_new_scale[index] % m_buffered_scale[index] % m_last_active_item % current_active_id;
        for (int i = 0; i < 3; ++i) {
            if (i != scale_sel) ImGui::ClearInputTextInitialData(label_scale_values[0][i], m_buffered_scale[i]);
            ImGui::ClearInputTextInitialData(label_scale_values[1][i], m_buffered_size[i]);
        }
    }

    if ((size_sel >= 0)) {
        // for (int index = 0; index < 3; index++)
        //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",after_index="<<index <<boost::format(",scale %1%, buffered %2%, original_id %3%, new_id %4%\n") %
        //    this->m_new_scale[index] % m_buffered_scale[index] % m_last_active_item % current_active_id;
        for (int i = 0; i < 3; ++i) {
            ImGui::ClearInputTextInitialData(label_scale_values[0][i], m_buffered_scale[i]);
            if (i != size_sel) ImGui::ClearInputTextInitialData(label_scale_values[1][i], m_buffered_size[i]);
        }
    }




    //send focus to m_glcanvas
    bool focued_on_text = false;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
        {
            unsigned int id = ImGui::GetID(label_scale_values[i][j]);
            if (current_active_id == id)
            {
                m_glcanvas.handle_sidebar_focus_event(label_scale_values[i][j] + 2, true);
                focued_on_text = true;
                break;
            }
        }
    if (!focued_on_text)
        m_glcanvas.handle_sidebar_focus_event("", false);

    m_last_active_item = current_active_id;

    last_scale_input_window_width = ImGui::GetWindowWidth();
    imgui_wrapper->end();

    //BBS
    ImGuiWrapper::pop_toolbar_style();
}


} //namespace GUI
} //namespace Slic3r
