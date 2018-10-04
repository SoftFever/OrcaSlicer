#include "GUI_ObjectManipulation.hpp"

#include "OptionsGroup.hpp"
#include "wxExtensions.hpp"
#include "Model.hpp"
#include "Geometry.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r
{
namespace GUI
{

OG_Settings::OG_Settings(wxWindow* parent, const bool staticbox)
{
    wxString title = staticbox ? " " : ""; // temporary workaround - #ys_FIXME
    m_og = std::make_shared<ConfigOptionsGroup>(parent, title);
}

wxSizer* OG_Settings::get_sizer()
{
    return m_og->sizer;
}

ObjectManipulation::ObjectManipulation(wxWindow* parent):
    OG_Settings(parent, true)
{
    m_og->set_name(_(L("Object Manipulation")));
    m_og->label_width = 100;
    m_og->set_grid_vgap(5);

    m_og->m_on_change = [this](t_config_option_key opt_key, boost::any value){
        if (opt_key == "scale_unit"){
            const wxString& selection = boost::any_cast<wxString>(value);
            std::vector<std::string> axes{ "x", "y", "z" };
            for (auto axis : axes) {
                std::string key = "scale_" + axis;
                get_optgroup(ogFrequentlyObjectSettings)->set_side_text(key, selection);
            }

            m_is_percent_scale = selection == _("%");
            update_scale_values();
        }
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
    def.width = 55;

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
        int def_value = 0;
        Line line = { _(option_name), "" };
        if (option_name == "Scale") {
            line.near_label_widget = [](wxWindow* parent) {
                auto btn = new PrusaLockButton(parent, wxID_ANY);
                btn->Bind(wxEVT_BUTTON, [btn](wxCommandEvent &event){
                    event.Skip();
                    wxTheApp->CallAfter([btn]() { set_uniform_scaling(btn->IsLocked()); });
                });
                return btn;
            };
        }

        ConfigOptionDef def;
        def.type = coInt;
        def.default_value = new ConfigOptionInt(def_value);
        def.width = 55;

        if (option_name == "Rotation")
            def.min = -360;

        const std::string lower_name = boost::algorithm::to_lower_copy(option_name);

        std::vector<std::string> axes{ "x", "y", "z" };
        for (auto axis : axes) {
            if (axis == "z" && option_name != "Scale")
                def.sidetext = sidetext;
            Option option = Option(def, lower_name + "_" + axis);
            option.opt.full_width = true;
            line.append_option(option);
        }

        if (option_name == "Scale")
        {
            def.width = 45;
            def.type = coStrings;
            def.gui_type = "select_open";
            def.enum_labels.push_back(L("%"));
            def.enum_labels.push_back(L("mm"));
            def.default_value = new ConfigOptionStrings{ "mm" };

            const Option option = Option(def, lower_name + "_unit");
            line.append_option(option);
        }

        return line;
    };


    // Settings table
    m_og->append_line(add_og_to_object_settings(L("Position"), L("mm")));
    m_og->append_line(add_og_to_object_settings(L("Rotation"), "°"));
    m_og->append_line(add_og_to_object_settings(L("Scale"), "mm"));


    def.label = L("Place on bed");
    def.type = coBool;
    def.tooltip = L("Automatic placing of models on printing bed in Y axis");
    def.gui_type = "";
    def.sidetext = "";
    def.default_value = new ConfigOptionBool{ false };
    m_og->append_single_option_line(Option(def, "place_on_bed"));

    m_extra_settings_sizer = new wxBoxSizer(wxVERTICAL);
    m_og->sizer->Add(m_extra_settings_sizer, 1, wxEXPAND | wxLEFT, 5);

    m_og->disable();
}

int ObjectManipulation::ol_selection()
{
    return wxGetApp().sidebar().get_ol_selection();
}

void ObjectManipulation::update_values()
{
    int selection = ol_selection();
    if (selection < 0 || wxGetApp().mainframe->m_plater->model().objects.size() <= selection) {
        m_og->set_value("position_x", 0);
        m_og->set_value("position_y", 0);
        m_og->set_value("position_z", 0);
        m_og->set_value("scale_x", 0);
        m_og->set_value("scale_y", 0);
        m_og->set_value("scale_z", 0);
        m_og->set_value("rotation_x", 0);
        m_og->set_value("rotation_y", 0);
        m_og->set_value("rotation_z", 0);
        m_og->disable();
        return;
    }
    m_is_percent_scale = boost::any_cast<wxString>(m_og->get_value("scale_unit")) == _("%");

    update_position_values();
    update_scale_values();
    update_rotation_values();
    m_og->enable();
}

void ObjectManipulation::update_scale_values()
{
    int selection = ol_selection();
    ModelObjectPtrs& objects = wxGetApp().mainframe->m_plater->model().objects;

    auto instance = objects[selection]->instances.front();
    auto size = objects[selection]->instance_bounding_box(0).size();

#if ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
    if (m_is_percent_scale) {
        m_og->set_value("scale_x", int(instance->get_scaling_factor(X) * 100));
        m_og->set_value("scale_y", int(instance->get_scaling_factor(Y) * 100));
        m_og->set_value("scale_z", int(instance->get_scaling_factor(Z) * 100));
    }
    else {
        m_og->set_value("scale_x", int(instance->get_scaling_factor(X) * size(0) + 0.5));
        m_og->set_value("scale_y", int(instance->get_scaling_factor(Y) * size(1) + 0.5));
        m_og->set_value("scale_z", int(instance->get_scaling_factor(Z) * size(2) + 0.5));
    }
#else
    if (m_is_percent_scale) {
        auto scale = instance->scaling_factor * 100.0;
        m_og->set_value("scale_x", int(scale));
        m_og->set_value("scale_y", int(scale));
        m_og->set_value("scale_z", int(scale));
    }
    else {
        m_og->set_value("scale_x", int(instance->scaling_factor * size(0) + 0.5));
        m_og->set_value("scale_y", int(instance->scaling_factor * size(1) + 0.5));
        m_og->set_value("scale_z", int(instance->scaling_factor * size(2) + 0.5));
    }
#endif // ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
}

void ObjectManipulation::update_position_values()
{
    auto instance = wxGetApp().mainframe->m_plater->model().objects[ol_selection()]->instances.front();

#if ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
    m_og->set_value("position_x", int(instance->get_offset(X)));
    m_og->set_value("position_y", int(instance->get_offset(Y)));
    m_og->set_value("position_z", int(instance->get_offset(Z)));
#else
    m_og->set_value("position_x", int(instance->offset(0)));
    m_og->set_value("position_y", int(instance->offset(1)));
    m_og->set_value("position_z", 0);
#endif // ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
}

void ObjectManipulation::update_position_values(const Vec3d& position)
{
    m_og->set_value("position_x", int(position(0)));
    m_og->set_value("position_y", int(position(1)));
    m_og->set_value("position_z", int(position(2)));
}

#if ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
void ObjectManipulation::update_scale_values(const Vec3d& scaling_factor)
{
    // this is temporary
    // to be able to update the values as size
    // we need to store somewhere the original size
    // or have it passed as parameter
    if (!m_is_percent_scale)
        m_og->set_value("scale_unit", _("%"));

    auto scale = scaling_factor * 100.0;
    m_og->set_value("scale_x", int(scale(0)));
    m_og->set_value("scale_y", int(scale(1)));
    m_og->set_value("scale_z", int(scale(2)));
}
#else
void ObjectManipulation::update_scale_values(double scaling_factor)
{
    // this is temporary
    // to be able to update the values as size
    // we need to store somewhere the original size
    // or have it passed as parameter
    if (!m_is_percent_scale)
        m_og->set_value("scale_unit", _("%"));

    auto scale = scaling_factor * 100.0;
    m_og->set_value("scale_x", int(scale));
    m_og->set_value("scale_y", int(scale));
    m_og->set_value("scale_z", int(scale));
}
#endif // ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM

void ObjectManipulation::update_rotation_values()
{
#if ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
    update_rotation_value(wxGetApp().mainframe->m_plater->model().objects[ol_selection()]->instances.front()->get_rotation());
#else
    auto instance = wxGetApp().mainframe->m_plater->model().objects[ol_selection()]->instances.front();
    m_og->set_value("rotation_x", 0);
    m_og->set_value("rotation_y", 0);
    m_og->set_value("rotation_z", int(Geometry::rad2deg(instance->rotation)));
#endif // ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
}

void ObjectManipulation::update_rotation_value(double angle, Axis axis)
{
    std::string axis_str;
    switch (axis) {
    case X: {
        axis_str = "rotation_x";
        break; }
    case Y: {
        axis_str = "rotation_y";
        break; }
    case Z: {
        axis_str = "rotation_z";
        break; }
    }

    m_og->set_value(axis_str, round_nearest(int(Geometry::rad2deg(angle)), 0));
}

#if ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM
void ObjectManipulation::update_rotation_value(const Vec3d& rotation)
{
    m_og->set_value("rotation_x", int(round_nearest(Geometry::rad2deg(rotation(0)), 0)));
    m_og->set_value("rotation_y", int(round_nearest(Geometry::rad2deg(rotation(1)), 0)));
    m_og->set_value("rotation_z", int(round_nearest(Geometry::rad2deg(rotation(2)), 0)));
}
#endif // ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM



} //namespace GUI
} //namespace Slic3r 