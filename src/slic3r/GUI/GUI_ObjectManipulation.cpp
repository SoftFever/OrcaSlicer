#include "GUI_ObjectManipulation.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"

#include "OptionsGroup.hpp"
#include "wxExtensions.hpp"
#include "PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Geometry.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r
{
namespace GUI
{

ObjectManipulation::ObjectManipulation(wxWindow* parent) :
    OG_Settings(parent, true)
#if ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
    , m_cache_position(Vec3d(DBL_MAX, DBL_MAX, DBL_MAX))
    , m_cache_rotation(Vec3d(DBL_MAX, DBL_MAX, DBL_MAX))
    , m_cache_scale(Vec3d(DBL_MAX, DBL_MAX, DBL_MAX))
    , m_cache_size(Vec3d(DBL_MAX, DBL_MAX, DBL_MAX))
#endif // ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
{
    m_og->set_name(_(L("Object Manipulation")));
    m_og->label_width = 100;
    m_og->set_grid_vgap(5);
    
    m_og->m_on_change = [this](const std::string& opt_key, const boost::any& value) {
        std::vector<std::string> axes{ "_x", "_y", "_z" };

        std::string param; 
        std::copy(opt_key.begin(), opt_key.end() - 2, std::back_inserter(param));

        size_t i = 0;
        Vec3d new_value;
        for (auto axis : axes)
            new_value(i++) = boost::any_cast<double>(m_og->get_value(param+axis));

        if (param == "position")
            change_position_value(new_value);
        else if (param == "rotation")
            change_rotation_value(new_value);
        else if (param == "scale")
#if ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
        {
            change_scale_value(new_value);
            update_settings_value(wxGetApp().plater()->canvas3D()->get_selection());
        }
#else
            change_scale_value(new_value);
#endif // ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
        else if (param == "size")
#if ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
        {
            change_size_value(new_value);
            update_settings_value(wxGetApp().plater()->canvas3D()->get_selection());
        }
#else
            change_size_value(new_value);
#endif // ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION

        wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(opt_key, false);
    };

    m_og->m_fill_empty_value = [this](const std::string& opt_key)
    {
#if !ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
        this->update_if_dirty();
#endif // !ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION

        std::string param;
        std::copy(opt_key.begin(), opt_key.end() - 2, std::back_inserter(param)); 

        double value = 0.0;

        if (param == "position") {
            int axis = opt_key.back() == 'x' ? 0 :
                       opt_key.back() == 'y' ? 1 : 2;

            value = m_cache_position(axis);
        }
        else if (param == "rotation") {
            int axis = opt_key.back() == 'x' ? 0 :
                opt_key.back() == 'y' ? 1 : 2;

            value = m_cache_rotation(axis);
        }
        else if (param == "scale") {
            int axis = opt_key.back() == 'x' ? 0 :
                opt_key.back() == 'y' ? 1 : 2;

            value = m_cache_scale(axis);
        }
        else if (param == "size") {
            int axis = opt_key.back() == 'x' ? 0 :
                opt_key.back() == 'y' ? 1 : 2;

            value = m_cache_size(axis);
        }

        m_og->set_value(opt_key, double_to_string(value));
        wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(opt_key, false);
    };

    m_og->m_set_focus = [this](const std::string& opt_key)
    {
#if !ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
        this->update_if_dirty();
#endif // !ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
        wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(opt_key, true);
    };

    ConfigOptionDef def;

    // Objects(sub-objects) name
    def.label = L("Name");
    // 	def.type = coString;
    def.gui_type = "legend";
    def.tooltip = L("Object name");
    def.full_width = true;
    def.default_value = new ConfigOptionString{ " " };
    m_og->append_single_option_line(Option(def, "object_name"));

    // Legend for object modification
    auto line = Line{ "", "" };
    def.label = "";
    def.type = coString;
    def.width = 50;

    std::vector<std::string> axes{ "x", "y", "z" };
    for (const auto axis : axes) {
        const auto label = boost::algorithm::to_upper_copy(axis);
        def.default_value = new ConfigOptionString{ "   " + label };
        Option option = Option(def, axis + "_axis_legend");
        line.append_option(option);
    }
    m_og->append_line(line);


    auto add_og_to_object_settings = [](const std::string& option_name, const std::string& sidetext)
    {
        Line line = { _(option_name), "" };
        ConfigOptionDef def;
        def.type = coFloat;
        def.default_value = new ConfigOptionFloat(0.0);
        def.width = 50;

        if (option_name == "Rotation")
        {
            def.min = -360;
            def.max = 360;
        }

        const std::string lower_name = boost::algorithm::to_lower_copy(option_name);

        std::vector<std::string> axes{ "x", "y", "z" };
        for (auto axis : axes) {
            if (axis == "z")
                def.sidetext = sidetext;
            Option option = Option(def, lower_name + "_" + axis);
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

    if (show && wxGetApp().get_view_mode() != ConfigMenuModeSimple) {
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
#if !ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
        update_if_dirty();
#endif // !ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
    }

    OG_Settings::UpdateAndShow(show);
}

void ObjectManipulation::update_settings_value(const GLCanvas3D::Selection& selection)
{
	m_new_move_label_string   = L("Position:");
    m_new_rotate_label_string = L("Rotation:");
    m_new_scale_label_string  = L("Scale factors:");
    if (selection.is_single_full_instance())
    {
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_instance_offset();
        m_new_rotation = volume->get_instance_rotation();
        m_new_scale    = volume->get_instance_scaling_factor();
        int obj_idx = volume->object_idx();
        if ((0 <= obj_idx) && (obj_idx < (int)wxGetApp().model_objects()->size()))
            m_new_size = volume->get_instance_transformation().get_matrix(true, true) * (*wxGetApp().model_objects())[obj_idx]->raw_mesh().bounding_box().size();
        else
            // this should never happen
            m_new_size = Vec3d::Zero();

        m_new_enabled  = true;
    }
    else if (selection.is_single_full_object())
    {
        const BoundingBoxf3& box = selection.get_bounding_box();
        m_new_position = box.center();
        m_new_rotation = Vec3d::Zero();
        m_new_scale    = Vec3d(1.0, 1.0, 1.0);
        m_new_size     = box.size();
        m_new_rotate_label_string = L("Rotate:");
		m_new_scale_label_string  = L("Scale:");
        m_new_enabled  = true;
    }
    else if (selection.is_single_modifier() || selection.is_single_volume())
    {
        // the selection contains a single volume
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_volume_offset();
        m_new_rotation = volume->get_volume_rotation();
        m_new_scale    = volume->get_volume_scaling_factor();
        m_new_size = volume->get_instance_transformation().get_matrix(true, true) * volume->get_volume_transformation().get_matrix(true, true) * volume->bounding_box.size();
        m_new_enabled  = true;
    }
    else if (wxGetApp().obj_list()->multiple_selection())
    {
        reset_settings_value();
		m_new_move_label_string   = L("Translate:");
		m_new_rotate_label_string = L("Rotate:");
		m_new_scale_label_string  = L("Scale:");
        m_new_size = selection.get_bounding_box().size();
        m_new_enabled  = true;
    }
    else
        reset_settings_value();

#if ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
    update_if_dirty();
#else
    m_dirty = true;
#endif // ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
}

void ObjectManipulation::update_if_dirty()
{
#if ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
    if (_(m_new_move_label_string) != m_move_Label->GetLabel())
        m_move_Label->SetLabel(_(m_new_move_label_string));

    if (_(m_new_rotate_label_string) != m_rotate_Label->GetLabel())
        m_rotate_Label->SetLabel(_(m_new_rotate_label_string));

    if (_(m_new_scale_label_string) != m_scale_Label->GetLabel())
        m_scale_Label->SetLabel(_(m_new_scale_label_string));

    if (m_cache_position(0) != m_new_position(0))
        m_og->set_value("position_x", double_to_string(m_new_position(0), 2));

    if (m_cache_position(1) != m_new_position(1))
        m_og->set_value("position_y", double_to_string(m_new_position(1), 2));

    if (m_cache_position(2) != m_new_position(2))
        m_og->set_value("position_z", double_to_string(m_new_position(2), 2));

    m_cache_position = m_new_position;

    auto scale = m_new_scale * 100.0;
    if (m_cache_scale(0) != scale(0))
        m_og->set_value("scale_x", double_to_string(scale(0), 2));

    if (m_cache_scale(1) != scale(1))
        m_og->set_value("scale_y", double_to_string(scale(1), 2));

    if (m_cache_scale(2) != scale(2))
        m_og->set_value("scale_z", double_to_string(scale(2), 2));

    m_cache_scale = scale;

    if (m_cache_size(0) != m_new_size(0))
        m_og->set_value("size_x", double_to_string(m_new_size(0), 2));

    if (m_cache_size(1) != m_new_size(1))
        m_og->set_value("size_y", double_to_string(m_new_size(1), 2));

    if (m_cache_size(2) != m_new_size(2))
        m_og->set_value("size_z", double_to_string(m_new_size(2), 2));

    m_cache_size = m_new_size;

    if (m_cache_rotation(0) != m_new_rotation(0))
        m_og->set_value("rotation_x", double_to_string(round_nearest(Geometry::rad2deg(m_new_rotation(0)), 0), 2));

    if (m_cache_rotation(1) != m_new_rotation(1))
        m_og->set_value("rotation_y", double_to_string(round_nearest(Geometry::rad2deg(m_new_rotation(1)), 0), 2));

    if (m_cache_rotation(2) != m_new_rotation(2))
        m_og->set_value("rotation_z", double_to_string(round_nearest(Geometry::rad2deg(m_new_rotation(2)), 0), 2));

    m_cache_rotation = m_new_rotation;

    if (m_new_enabled)
        m_og->enable();
    else
        m_og->disable();
#else
    if (! m_dirty)
        return;

    m_move_Label->SetLabel(_(m_new_move_label_string));
    m_rotate_Label->SetLabel(_(m_new_rotate_label_string));
    m_scale_Label->SetLabel(_(m_new_scale_label_string));

    m_og->set_value("position_x", double_to_string(m_new_position(0), 2));
    m_og->set_value("position_y", double_to_string(m_new_position(1), 2));
    m_og->set_value("position_z", double_to_string(m_new_position(2), 2));
    m_cache_position = m_new_position;

    auto scale = m_new_scale * 100.0;
    m_og->set_value("scale_x", double_to_string(scale(0), 2));
    m_og->set_value("scale_y", double_to_string(scale(1), 2));
    m_og->set_value("scale_z", double_to_string(scale(2), 2));
    m_cache_scale = scale;

    m_og->set_value("size_x", double_to_string(m_new_size(0), 2));
    m_og->set_value("size_y", double_to_string(m_new_size(1), 2));
    m_og->set_value("size_z", double_to_string(m_new_size(2), 2));
    m_cache_size = m_new_size;

    m_og->set_value("rotation_x", double_to_string(round_nearest(Geometry::rad2deg(m_new_rotation(0)), 0), 2));
    m_og->set_value("rotation_y", double_to_string(round_nearest(Geometry::rad2deg(m_new_rotation(1)), 0), 2));
    m_og->set_value("rotation_z", double_to_string(round_nearest(Geometry::rad2deg(m_new_rotation(2)), 0), 2));
    m_cache_rotation = m_new_rotation;

    if (m_new_enabled)
        m_og->enable();
    else
        m_og->disable();

    m_dirty = false;
#endif // ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
}

void ObjectManipulation::reset_settings_value()
{
    m_new_position  = Vec3d::Zero();
    m_new_rotation  = Vec3d::Zero();
    m_new_scale = Vec3d::Ones();
    m_new_size = Vec3d::Zero();
    m_new_enabled   = false;
#if !ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
    m_dirty = true;
#endif // !ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
}

void ObjectManipulation::change_position_value(const Vec3d& position)
{
    auto canvas = wxGetApp().plater()->canvas3D();
    GLCanvas3D::Selection& selection = canvas->get_selection();
    selection.start_dragging();
    selection.translate(position - m_cache_position, selection.requires_local_axes());
    canvas->do_move();

    m_cache_position = position;
}

void ObjectManipulation::change_rotation_value(const Vec3d& rotation)
{
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    const GLCanvas3D::Selection& selection = canvas->get_selection();

    Vec3d rad_rotation;
    for (size_t i = 0; i < 3; ++i)
    {
        rad_rotation(i) = Geometry::deg2rad(rotation(i));
    }

    canvas->get_selection().start_dragging();
    canvas->get_selection().rotate(rad_rotation, selection.is_single_full_instance());
    canvas->do_rotate();

#if ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
    m_cache_rotation = rotation;
#endif // ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
}

void ObjectManipulation::change_scale_value(const Vec3d& scale)
{
    Vec3d scaling_factor = scale;
    const GLCanvas3D::Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    if (selection.requires_uniform_scale())
    {
        Vec3d abs_scale_diff = (scale - m_cache_scale).cwiseAbs();
        double max_diff = abs_scale_diff(X);
        Axis max_diff_axis = X;
        if (max_diff < abs_scale_diff(Y))
        {
            max_diff = abs_scale_diff(Y);
            max_diff_axis = Y;
        }
        if (max_diff < abs_scale_diff(Z))
        {
            max_diff = abs_scale_diff(Z);
            max_diff_axis = Z;
        }
        scaling_factor = Vec3d(scale(max_diff_axis), scale(max_diff_axis), scale(max_diff_axis));
    }

    scaling_factor *= 0.01;

    auto canvas = wxGetApp().plater()->canvas3D();
    canvas->get_selection().start_dragging();
    canvas->get_selection().scale(scaling_factor, false);
    canvas->do_scale();

#if ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
    m_cache_scale = scale;
#endif // ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
}

void ObjectManipulation::change_size_value(const Vec3d& size)
{
    const GLCanvas3D::Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();

    Vec3d ref_size = m_cache_size;
    if (selection.is_single_full_instance())
    {
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        ref_size = volume->bounding_box.size();
    }

    change_scale_value(100.0 * Vec3d(size(0) / ref_size(0), size(1) / ref_size(1), size(2) / ref_size(2)));

#if ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
    m_cache_size = size;
#endif // ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION
}

} //namespace GUI
} //namespace Slic3r 