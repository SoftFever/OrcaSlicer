#include "GUI_ObjectManipulation.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"

#include "OptionsGroup.hpp"
#include "wxExtensions.hpp"
#include "PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Geometry.hpp"
#include "Selection.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r
{
namespace GUI
{

ObjectManipulation::ObjectManipulation(wxWindow* parent) :
    OG_Settings(parent, true)
#ifndef __APPLE__
    , m_focused_option("")
#endif // __APPLE__
{
    m_og->set_name(_(L("Object Manipulation")));
    m_og->label_width = 12 * wxGetApp().em_unit();//125;
    m_og->set_grid_vgap(5);
    
    m_og->m_on_change = std::bind(&ObjectManipulation::on_change, this, std::placeholders::_1, std::placeholders::_2);
    m_og->m_fill_empty_value = std::bind(&ObjectManipulation::on_fill_empty_value, this, std::placeholders::_1);

    m_og->m_set_focus = [this](const std::string& opt_key)
    {
#ifndef __APPLE__
        m_focused_option = opt_key;
#endif // __APPLE__

        // needed to show the visual hints in 3D scene
        wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(opt_key, true);
    };

    ConfigOptionDef def;

    // Objects(sub-objects) name
    def.label = L("Name");
    def.gui_type = "legend";
    def.tooltip = L("Object name");
    def.width = 21 * wxGetApp().em_unit();
    def.default_value = new ConfigOptionString{ " " };
    m_og->append_single_option_line(Option(def, "object_name"));

    const int field_width = 5 * wxGetApp().em_unit()/*50*/;

    // Legend for object modification
    auto line = Line{ "", "" };
    def.label = "";
    def.type = coString;
    def.width = field_width/*50*/;

	for (const std::string axis : { "x", "y", "z" }) {
        const std::string label = boost::algorithm::to_upper_copy(axis);
        def.default_value = new ConfigOptionString{ "   " + label };
        Option option = Option(def, axis + "_axis_legend");
        line.append_option(option);
    }
    m_og->append_line(line);

    auto add_og_to_object_settings = [this, field_width](const std::string& option_name, const std::string& sidetext)
    {
        Line line = { _(option_name), "" };
        ConfigOptionDef def;
        def.type = coFloat;
        def.default_value = new ConfigOptionFloat(0.0);
        def.width = field_width/*50*/;

        // Add "uniform scaling" button in front of "Scale" option 
        if (option_name == "Scale") {
            line.near_label_widget = [this](wxWindow* parent) {
                auto btn = new PrusaLockButton(parent, wxID_ANY);
                btn->Bind(wxEVT_BUTTON, [btn, this](wxCommandEvent &event){
                    event.Skip();
                    wxTheApp->CallAfter([btn, this]() { set_uniform_scaling(btn->IsLocked()); });
                });
                m_lock_bnt = btn;
                return btn;
            };
        }

        // Add empty bmp (Its size have to be equal to PrusaLockButton) in front of "Size" option to label alignment
        else if (option_name == "Size") {
            line.near_label_widget = [this](wxWindow* parent) {
                return new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition,
                                          create_scaled_bitmap(m_parent, "one_layer_lock_on.png").GetSize());
            };
        }

        const std::string lower_name = boost::algorithm::to_lower_copy(option_name);

        for (const char *axis : { "_x", "_y", "_z" }) {
            if (axis[1] == 'z')
                def.sidetext = sidetext;
            Option option = Option(def, lower_name + axis);
            option.opt.full_width = true;
            line.append_option(option);
        }

        return line;
    };


    // Settings table
    m_og->append_line(add_og_to_object_settings(L("Position"), L("mm")), &m_move_Label);
    m_og->append_line(add_og_to_object_settings(L("Rotation"), "°"), &m_rotate_Label);
    m_og->append_line(add_og_to_object_settings(L("Scale"), "%"), &m_scale_Label);
    m_og->append_line(add_og_to_object_settings(L("Size"), "mm"));

    /* Unused parameter at this time
    def.label = L("Place on bed");
    def.type = coBool;
    def.tooltip = L("Automatic placing of models on printing bed in Y axis");
    def.gui_type = "";
    def.sidetext = "";
    def.default_value = new ConfigOptionBool{ false };
    m_og->append_single_option_line(Option(def, "place_on_bed"));
    */
}

void ObjectManipulation::Show(const bool show)
{
    if (show == IsShown())
        return;

    m_og->Show(show);

    if (show && wxGetApp().get_mode() != comSimple) {
        m_og->get_grid_sizer()->Show(size_t(0), false);
        m_og->get_grid_sizer()->Show(size_t(1), false);
    }
}

bool ObjectManipulation::IsShown()
{
    return m_og->get_grid_sizer()->IsShown(2);
}

void ObjectManipulation::UpdateAndShow(const bool show)
{
    if (show) {
        update_settings_value(wxGetApp().plater()->canvas3D()->get_selection());
    }

    OG_Settings::UpdateAndShow(show);
}

void ObjectManipulation::update_settings_value(const Selection& selection)
{
	m_new_move_label_string   = L("Position");
    m_new_rotate_label_string = L("Rotation");
    m_new_scale_label_string  = L("Scale factors");

    ObjectList* obj_list = wxGetApp().obj_list();
    if (selection.is_single_full_instance() && ! m_world_coordinates)
    {
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_instance_offset();
        m_new_rotation = volume->get_instance_rotation();
        m_new_scale    = volume->get_instance_scaling_factor();
        int obj_idx = volume->object_idx();
        int instance_idx = volume->instance_idx();
        if ((0 <= obj_idx) && (obj_idx < (int)wxGetApp().model_objects()->size()))
        {
            bool changed_box = false;
            //FIXME matching an object idx may not be enough
            if (!m_cache.instance.matches_object(obj_idx))
            {
                m_cache.instance.set(obj_idx, instance_idx, (*wxGetApp().model_objects())[obj_idx]->raw_mesh_bounding_box().size());
                changed_box = true;
            }
            //FIXME matching an instance idx may not be enough. Check for ModelObject id an all ModelVolume ids.
            if (changed_box || !m_cache.instance.matches_instance(instance_idx) || !m_cache.scale.isApprox(100.0 * m_new_scale))
                m_new_size = volume->get_instance_transformation().get_scaling_factor().cwiseProduct(m_cache.instance.box_size);
        }
        else {
            // this should never happen
            assert(false);
            m_new_size = Vec3d::Zero();
        }

        m_new_enabled  = true;
    }
    else if ((selection.is_single_full_instance() && m_world_coordinates) ||
             (selection.is_single_full_object() && obj_list->is_selected(itObject)))
    {
        m_cache.instance.reset();

        const BoundingBoxf3& box = selection.get_bounding_box();
        m_new_position = box.center();
        m_new_rotation = Vec3d::Zero();
        m_new_scale    = Vec3d(1.0, 1.0, 1.0);
        m_new_size     = box.size();
        m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_enabled  = true;
    }
    else if (selection.is_single_modifier() || selection.is_single_volume())
    {
        m_cache.instance.reset();

        // the selection contains a single volume
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_volume_offset();
        m_new_rotation = volume->get_volume_rotation();
        m_new_scale    = volume->get_volume_scaling_factor();
        m_new_size = volume->get_volume_transformation().get_scaling_factor().cwiseProduct(volume->bounding_box.size());
        m_new_enabled = true;
    }
    else if (obj_list->multiple_selection() || obj_list->is_selected(itInstanceRoot))
    {
        reset_settings_value();
		m_new_move_label_string   = L("Translate");
		m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_size = selection.get_bounding_box().size();
        m_new_enabled  = true;
    }
	else {
        // No selection, reset the cache.
		assert(selection.is_empty());
		reset_settings_value();
	}

    m_dirty = true;
}

void ObjectManipulation::update_if_dirty()
{
    if (!m_dirty)
        return;

    auto update_label = [](std::string &label_cache, const std::string &new_label, wxStaticText *widget) {
        std::string new_label_localized = _(new_label) + ":";
        if (label_cache != new_label_localized) {
            label_cache = new_label_localized;
            widget->SetLabel(new_label_localized);
        }
    };
    update_label(m_cache.move_label_string,   m_new_move_label_string,   m_move_Label);
    update_label(m_cache.rotate_label_string, m_new_rotate_label_string, m_rotate_Label);
    update_label(m_cache.scale_label_string,  m_new_scale_label_string,  m_scale_Label);

	Vec3d scale = m_new_scale * 100.0;
    Vec3d deg_rotation = (180.0 / M_PI) * m_new_rotation;

    char axis[2] = "x";
    for (int i = 0; i < 3; ++ i, ++ axis[0]) {
        if (m_cache.position(i) != m_new_position(i))
            m_og->set_value(std::string("position_") + axis, double_to_string(m_new_position(i), 2));
        if (m_cache.scale(i) != scale(i))
            m_og->set_value(std::string("scale_") + axis, double_to_string(scale(i), 2));
        if (m_cache.size(i) != m_new_size(i))
            m_og->set_value(std::string("size_") + axis, double_to_string(m_new_size(i), 2));
        if (m_cache.rotation(i) != m_new_rotation(i) || m_new_rotation(i) == 0.0)
            m_og->set_value(std::string("rotation_") + axis, double_to_string(deg_rotation(i), 2));
    }

    m_cache.position = m_new_position;
    m_cache.scale = scale;
    m_cache.size = m_new_size;
    m_cache.rotation = deg_rotation;

    if (wxGetApp().plater()->canvas3D()->get_selection().requires_uniform_scale()) {
        m_lock_bnt->SetLock(true);
        m_lock_bnt->Disable();
    }
    else {
        m_lock_bnt->SetLock(m_uniform_scale);
        m_lock_bnt->Enable();
    }

    if (m_new_enabled)
        m_og->enable();
    else
        m_og->disable();

    m_dirty = false;
}

#ifndef __APPLE__
void ObjectManipulation::emulate_kill_focus()
{
    if (m_focused_option.empty())
        return;

    // we need to use a copy because the value of m_focused_option is modified inside on_change() and on_fill_empty_value()
    std::string option = m_focused_option;

    // see TextCtrl::propagate_value()
    if (static_cast<wxTextCtrl*>(m_og->get_fieldc(option, 0)->getWindow())->GetValue().empty())
        on_fill_empty_value(option);
    else
        on_change(option, 0);
}
#endif // __APPLE__

void ObjectManipulation::reset_settings_value()
{
    m_new_position = Vec3d::Zero();
    m_new_rotation = Vec3d::Zero();
    m_new_scale = Vec3d::Ones();
    m_new_size = Vec3d::Zero();
    m_new_enabled = false;
    m_cache.instance.reset();
    m_dirty = true;
}

void ObjectManipulation::change_position_value(const Vec3d& position)
{
    auto canvas = wxGetApp().plater()->canvas3D();
    Selection& selection = canvas->get_selection();
    selection.start_dragging();
    selection.translate(position - m_cache.position, selection.requires_local_axes());
    canvas->do_move();

    m_cache.position = position;
}

void ObjectManipulation::change_rotation_value(const Vec3d& rotation)
{
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    Selection& selection = canvas->get_selection();

    TransformationType transformation_type(TransformationType::World_Relative_Joint);
    if (selection.is_single_full_instance() || selection.requires_local_axes())
		transformation_type.set_independent();
	if (selection.is_single_full_instance() && ! m_world_coordinates) {
        //FIXME Selection::rotate() does not process absoulte rotations correctly: It does not recognize the axis index, which was changed.
		// transformation_type.set_absolute();
		transformation_type.set_local();
	}

    selection.start_dragging();
	selection.rotate(
		(M_PI / 180.0) * (transformation_type.absolute() ? rotation : rotation - m_cache.rotation), 
		transformation_type);
    canvas->do_rotate();

    m_cache.rotation = rotation;
}

void ObjectManipulation::change_scale_value(const Vec3d& scale)
{
    this->do_scale(scale);

    if (!m_cache.scale.isApprox(scale))
        m_cache.instance.instance_idx = -1;

    m_cache.scale = scale;
}

void ObjectManipulation::change_size_value(const Vec3d& size)
{
    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();

    Vec3d ref_size = m_cache.size;
    if (selection.is_single_volume() || selection.is_single_modifier())
    {
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        ref_size = volume->bounding_box.size();
    }
    else if (selection.is_single_full_instance() && ! m_world_coordinates)
        ref_size = m_cache.instance.box_size;

    this->do_scale(100.0 * Vec3d(size(0) / ref_size(0), size(1) / ref_size(1), size(2) / ref_size(2)));

    m_cache.size = size;
}

void ObjectManipulation::do_scale(const Vec3d &scale) const
{
    Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    Vec3d scaling_factor = scale;

    if (m_uniform_scale || selection.requires_uniform_scale())
    {
        int max_diff_axis;
        (scale - m_cache.scale).cwiseAbs().maxCoeff(&max_diff_axis);
        scaling_factor = scale(max_diff_axis) * Vec3d::Ones();
    }

    TransformationType transformation_type(TransformationType::World_Relative_Joint);
    if (selection.is_single_full_instance() && ! m_world_coordinates)
        transformation_type.set_local();
    selection.start_dragging();
    selection.scale(scaling_factor * 0.01, transformation_type);
    wxGetApp().plater()->canvas3D()->do_scale();
}

void ObjectManipulation::on_change(t_config_option_key opt_key, const boost::any& value)
{
    // needed to hide the visual hints in 3D scene
    wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(opt_key, false);
#ifndef __APPLE__
    m_focused_option = "";
#endif // __APPLE__

    if (!m_cache.is_valid())
        return;

    // Value of all three axes of the position / rotation / scale / size is extracted.
    Vec3d new_value;
    opt_key.back() = 'x';
	for (int i = 0; i < 3; ++ i, ++ opt_key.back())
		new_value(i) = boost::any_cast<double>(m_og->get_value(opt_key));

    if (boost::starts_with(opt_key, "position_"))
        change_position_value(new_value);
    else if (boost::starts_with(opt_key, "rotation_"))
        change_rotation_value(new_value);
    else if (boost::starts_with(opt_key, "scale_"))
        change_scale_value(new_value);
    else if (boost::starts_with(opt_key, "size_"))
        change_size_value(new_value);
}

void ObjectManipulation::on_fill_empty_value(const std::string& opt_key)
{
    // needed to hide the visual hints in 3D scene
    wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(opt_key, false);
#ifndef __APPLE__
    m_focused_option = "";
#endif // __APPLE__

    if (!m_cache.is_valid())
        return;

    const Vec3d *vec = nullptr;
	if (boost::starts_with(opt_key, "position_"))
		vec = &m_cache.position;
	else if (boost::starts_with(opt_key, "rotation_"))
		vec = &m_cache.rotation;
	else if (boost::starts_with(opt_key, "scale_"))
		vec = &m_cache.scale;
	else if (boost::starts_with(opt_key, "size_"))
		vec = &m_cache.size;
	else
		assert(false);

	if (vec != nullptr)
		m_og->set_value(opt_key, double_to_string((*vec)(opt_key.back() - 'x')));
}

} //namespace GUI
} //namespace Slic3r 
