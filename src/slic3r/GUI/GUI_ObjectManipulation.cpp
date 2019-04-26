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

static wxBitmapComboBox* create_word_local_combo(wxWindow *parent)
{
    wxSize size(15 * wxGetApp().em_unit(), -1);

    wxBitmapComboBox *temp = nullptr;
#ifdef __WXOSX__
    /* wxBitmapComboBox with wxCB_READONLY style return NULL for GetTextCtrl(),
     * so ToolTip doesn't shown.
     * Next workaround helps to solve this problem
     */
    temp = new wxBitmapComboBox();
    temp->SetTextCtrlStyle(wxTE_READONLY);
	temp->Create(parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr);
#else
	temp = new wxBitmapComboBox(parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr, wxCB_READONLY);
#endif //__WXOSX__

    temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    temp->SetBackgroundStyle(wxBG_STYLE_PAINT);

    temp->Append(_(L("World")));
    temp->Append(_(L("Local")));
    temp->SetSelection(0);
    temp->SetValue(temp->GetString(0));

#ifndef __WXGTK__
    /* Workaround for a correct rendering of the control without Bitmap (under MSW and OSX):
     * 
     * 1. We should create small Bitmap to fill Bitmaps RefData,
     *    ! in this case wxBitmap.IsOK() return true.
     * 2. But then set width to 0 value for no using of bitmap left and right spacing 
     * 3. Set this empty bitmap to the at list one item and BitmapCombobox will be recreated correct
     * 
     * Note: Set bitmap height to the Font size because of OSX rendering.
     */
    wxBitmap empty_bmp(1, temp->GetFont().GetPixelSize().y + 2);
    empty_bmp.SetWidth(0);
    temp->SetItemBitmap(0, empty_bmp);
#endif

    temp->SetToolTip(_(L("Select coordinate space, in which the transformation will be performed.")));
	return temp;
}

ObjectManipulation::ObjectManipulation(wxWindow* parent) :
    OG_Settings(parent, true)
#ifndef __APPLE__
    , m_focused_option("")
#endif // __APPLE__
{
    m_og->set_name(_(L("Object Manipulation")));
    m_og->label_width = 12;//125;
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
    def.width = 21;
    def.default_value = new ConfigOptionString{ " " };
    m_og->append_single_option_line(Option(def, "object_name"));

    const int field_width = 5;

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
    line.near_label_widget = [this](wxWindow* parent) {
        wxBitmapComboBox *combo = create_word_local_combo(parent);
		combo->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent &evt) { this->set_world_coordinates(evt.GetSelection() != 1); }), combo->GetId());
        m_word_local_combo = combo;
        return combo;
    };
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
                auto btn = new LockButton(parent, wxID_ANY);
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

    // call back for a rescale of button "Set uniform scale"
    m_og->rescale_near_label_widget = [this](wxWindow* win) {
        auto *ctrl = dynamic_cast<LockButton*>(win);
        if (ctrl == nullptr)
            return;
        ctrl->msw_rescale();
    };
}

void ObjectManipulation::Show(const bool show)
{
	if (show != IsShown()) {
		m_og->Show(show);

		if (show && wxGetApp().get_mode() != comSimple) {
			m_og->get_grid_sizer()->Show(size_t(0), false);
			m_og->get_grid_sizer()->Show(size_t(1), false);
		}
	}

	if (show) {
		bool show_world_local_combo = wxGetApp().plater()->canvas3D()->get_selection().is_single_full_instance();
		m_word_local_combo->Show(show_world_local_combo);
	}
}

bool ObjectManipulation::IsShown()
{
    return m_og->get_grid_sizer()->IsShown(2);
}

void ObjectManipulation::UpdateAndShow(const bool show)
{
    if (show)
        update_settings_value(wxGetApp().plater()->canvas3D()->get_selection());

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
        m_new_rotation = volume->get_instance_rotation() * (180. / M_PI);
        m_new_scale    = volume->get_instance_scaling_factor() * 100.;
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
            if (changed_box || !m_cache.instance.matches_instance(instance_idx) || !m_cache.scale.isApprox(m_new_scale))
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
        m_new_scale    = Vec3d(100., 100., 100.);
        m_new_size     = box.size();
        m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_enabled  = true;

		if (selection.is_single_full_instance() && m_world_coordinates && ! m_uniform_scale) {
			// Verify whether the instance rotation is multiples of 90 degrees, so that the scaling in world coordinates is possible.
			// all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
			const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
			// Is the angle close to a multiple of 90 degrees?
			if (! Geometry::is_rotation_ninety_degrees(volume->get_instance_rotation())) {
				// Manipulating an instance in the world coordinate system, rotation is not multiples of ninety degrees, therefore enforce uniform scaling.
				m_uniform_scale = true;
				m_lock_bnt->SetLock(true);
			}
		}
    }
    else if (selection.is_single_modifier() || selection.is_single_volume())
    {
        m_cache.instance.reset();

        // the selection contains a single volume
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_volume_offset();
        m_new_rotation = volume->get_volume_rotation() * (180. / M_PI);
        m_new_scale    = volume->get_volume_scaling_factor() * 100.;
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
//		assert(selection.is_empty());
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

    char axis[2] = "x";
    for (int i = 0; i < 3; ++ i, ++ axis[0]) {
        auto update = [this, i, &axis](Vec3d &cached, Vec3d &cached_rounded, const char *key, const Vec3d &new_value) {
			wxString new_text = double_to_string(new_value(i), 2);
			double new_rounded;
			new_text.ToDouble(&new_rounded);
			if (std::abs(cached_rounded(i) - new_rounded) > EPSILON) {
				cached_rounded(i) = new_rounded;
                m_og->set_value(std::string(key) + axis, new_text);
            }
			cached(i) = new_value(i);
		};
        update(m_cache.position, m_cache.position_rounded, "position_", m_new_position);
        update(m_cache.scale,    m_cache.scale_rounded,    "scale_",    m_new_scale);
        update(m_cache.size,     m_cache.size_rounded,     "size_",     m_new_size);
        update(m_cache.rotation, m_cache.rotation_rounded, "rotation_", m_new_rotation);
    }

    if (wxGetApp().plater()->canvas3D()->get_selection().requires_uniform_scale()) {
        m_lock_bnt->SetLock(true);
        m_lock_bnt->Disable();
    }
    else {
        m_lock_bnt->SetLock(m_uniform_scale);
        m_lock_bnt->Enable();
    }

    { 
        int new_selection = m_world_coordinates ? 0 : 1; 
        if (m_word_local_combo->GetSelection() != new_selection)
            m_word_local_combo->SetSelection(new_selection);
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

void ObjectManipulation::change_position_value(int axis, double value)
{
    if (std::abs(m_cache.position_rounded(axis) - value) < EPSILON)
        return;

    Vec3d position = m_cache.position;
    position(axis) = value;

    auto canvas = wxGetApp().plater()->canvas3D();
    Selection& selection = canvas->get_selection();
    selection.start_dragging();
    selection.translate(position - m_cache.position, selection.requires_local_axes());
    canvas->do_move();

    m_cache.position = position;
	m_cache.position_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void ObjectManipulation::change_rotation_value(int axis, double value)
{
    if (std::abs(m_cache.rotation_rounded(axis) - value) < EPSILON)
        return;

    Vec3d rotation = m_cache.rotation;
    rotation(axis) = value;

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
	m_cache.rotation_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}

void ObjectManipulation::change_scale_value(int axis, double value)
{
    if (std::abs(m_cache.scale_rounded(axis) - value) < EPSILON)
        return;

    Vec3d scale = m_cache.scale;
    scale(axis) = value;

    this->do_scale(scale);

    if (!m_cache.scale.isApprox(scale))
        m_cache.instance.instance_idx = -1;

    m_cache.scale = scale;
	m_cache.scale_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}

void ObjectManipulation::change_size_value(int axis, double value)
{
    if (std::abs(m_cache.size_rounded(axis) - value) < EPSILON)
        return;

    Vec3d size = m_cache.size;
    size(axis) = value;

    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();

    Vec3d ref_size = m_cache.size;
    if (selection.is_single_volume() || selection.is_single_modifier())
    {
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        ref_size = volume->bounding_box.size();
    }
    else if (selection.is_single_full_instance() && ! m_world_coordinates)
        ref_size = m_cache.instance.box_size;

    this->do_scale(100. * Vec3d(size(0) / ref_size(0), size(1) / ref_size(1), size(2) / ref_size(2)));

    m_cache.size = size;
	m_cache.size_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
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

    int    axis      = opt_key.back() - 'x';
    double new_value = boost::any_cast<double>(m_og->get_value(opt_key));

    if (boost::starts_with(opt_key, "position_"))
        change_position_value(axis, new_value);
    else if (boost::starts_with(opt_key, "rotation_"))
        change_rotation_value(axis, new_value);
    else if (boost::starts_with(opt_key, "scale_"))
        change_scale_value(axis, new_value);
    else if (boost::starts_with(opt_key, "size_"))
        change_size_value(axis, new_value);
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
    Vec3d       *rounded = nullptr;
	if (boost::starts_with(opt_key, "position_")) {
		vec = &m_cache.position;
        rounded = &m_cache.position_rounded;
    } else if (boost::starts_with(opt_key, "rotation_")) {
		vec = &m_cache.rotation;
        rounded = &m_cache.rotation_rounded;
    } else if (boost::starts_with(opt_key, "scale_")) {
		vec = &m_cache.scale;
        rounded = &m_cache.scale_rounded;
    } else if (boost::starts_with(opt_key, "size_")) {
		vec = &m_cache.size;
        rounded = &m_cache.size_rounded;
    } else
		assert(false);

	if (vec != nullptr) {
        int axis = opt_key.back() - 'x';
        wxString new_text = double_to_string((*vec)(axis));
		m_og->set_value(opt_key, new_text);
		new_text.ToDouble(&(*rounded)(axis));
    }
}

void ObjectManipulation::set_uniform_scaling(const bool new_value)
{ 
    const Selection &selection = wxGetApp().plater()->canvas3D()->get_selection();
	if (selection.is_single_full_instance() && m_world_coordinates && !new_value) {
        // Verify whether the instance rotation is multiples of 90 degrees, so that the scaling in world coordinates is possible.
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        // Is the angle close to a multiple of 90 degrees?
		if (! Geometry::is_rotation_ninety_degrees(volume->get_instance_rotation())) {
            // Cannot apply scaling in the world coordinate system.
			wxMessageDialog dlg(GUI::wxGetApp().mainframe,
                _(L("Non-uniform scaling of tilted objects is not supported in the World coordinate system.\n"
                    "Do you want to rotate the mesh?")),
                SLIC3R_APP_NAME,
                wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
            if (dlg.ShowModal() != wxID_YES) {
                // Enforce uniform scaling.
                m_lock_bnt->SetLock(true);
                return;
            }
            // Bake the rotation into the meshes of the object.
            (*wxGetApp().model_objects())[volume->composite_id.object_id]->bake_xy_rotation_into_meshes(volume->composite_id.instance_id);
            // Update the 3D scene, selections etc.
            wxGetApp().plater()->update();
        }
    }
    m_uniform_scale = new_value;
}

} //namespace GUI
} //namespace Slic3r 
