// #include "libslic3r/GCodeSender.hpp"
#include "ConfigManipulation.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "MsgDialog.hpp"

#include <wx/msgdlg.h>

namespace Slic3r {
namespace GUI {

void ConfigManipulation::apply(DynamicPrintConfig* config, DynamicPrintConfig* new_config)
{
    bool modified = false;
    for (auto opt_key : config->diff(*new_config)) {
        config->set_key_value(opt_key, new_config->option(opt_key)->clone());
        modified = true;
    }

    if (modified && load_config != nullptr)
        load_config();
}

void ConfigManipulation::toggle_field(const std::string& opt_key, const bool toggle, int opt_index/* = -1*/)
{
    if (local_config) {
        if (local_config->option(opt_key) == nullptr)
            return;
    }
    cb_toggle_field(opt_key, toggle, opt_index);
}

void ConfigManipulation::update_print_fff_config(DynamicPrintConfig* config, const bool is_global_config)
{
    // #ys_FIXME_to_delete
    //! Temporary workaround for the correct updates of the TextCtrl (like "layer_height"):
    // KillFocus() for the wxSpinCtrl use CallAfter function. So,
    // to except the duplicate call of the update() after dialog->ShowModal(),
    // let check if this process is already started.
    if (is_msg_dlg_already_exist)
        return;

    // layer_height shouldn't be equal to zero
    if (config->opt_float("layer_height") < EPSILON)
    {
        const wxString msg_text = _(L("Layer height is not valid.\n\nThe layer height will be reset to 0.01."));
        //wxMessageDialog dialog(nullptr, msg_text, _(L("Layer height")), wxICON_WARNING | wxOK);
        MessageDialog dialog(nullptr, msg_text, _(L("Layer height")), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.01));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (config->option<ConfigOptionFloatOrPercent>("first_layer_height")->value < EPSILON)
    {
        const wxString msg_text = _(L("First layer height is not valid.\n\nThe first layer height will be reset to 0.01."));
        //wxMessageDialog dialog(nullptr, msg_text, _(L("First layer height")), wxICON_WARNING | wxOK);
        MessageDialog dialog(nullptr, msg_text, _(L("First layer height")), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(0.01, false));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    double fill_density = config->option<ConfigOptionPercent>("fill_density")->value;

    if (config->opt_bool("spiral_vase") &&
        ! (config->opt_int("perimeters") == 1 && 
           config->opt_int("top_solid_layers") == 0 &&
           fill_density == 0 &&
           ! config->opt_bool("support_material") &&
           config->opt_int("support_material_enforce_layers") == 0 &&
           config->opt_bool("ensure_vertical_shell_thickness") &&
           ! config->opt_bool("thin_walls")))
    {
        wxString msg_text = _(L("The Spiral Vase mode requires:\n"
                                "- one perimeter\n"
                                "- no top solid layers\n"
                                "- 0% fill density\n"
                                "- no support material\n"
                                "- Ensure vertical shell thickness enabled\n"
               					"- Detect thin walls disabled"));
        if (is_global_config)
            msg_text += "\n\n" + _(L("Shall I adjust those settings in order to enable Spiral Vase?"));
        //wxMessageDialog dialog(nullptr, msg_text, _(L("Spiral Vase")),
        MessageDialog dialog(nullptr, msg_text, _(L("Spiral Vase")),
                               wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
        DynamicPrintConfig new_conf = *config;
        auto answer = dialog.ShowModal();
        bool support = true;
        if (!is_global_config || answer == wxID_YES) {
            new_conf.set_key_value("perimeters", new ConfigOptionInt(1));
            new_conf.set_key_value("top_solid_layers", new ConfigOptionInt(0));
            new_conf.set_key_value("fill_density", new ConfigOptionPercent(0));
            new_conf.set_key_value("support_material", new ConfigOptionBool(false));
            new_conf.set_key_value("support_material_enforce_layers", new ConfigOptionInt(0));
            new_conf.set_key_value("ensure_vertical_shell_thickness", new ConfigOptionBool(true));
            new_conf.set_key_value("thin_walls", new ConfigOptionBool(false));            
            fill_density = 0;
            support = false;
        }
        else {
            new_conf.set_key_value("spiral_vase", new ConfigOptionBool(false));
        }
        apply(config, &new_conf);
        if (cb_value_change) {
            cb_value_change("fill_density", fill_density);
            if (!support)
                cb_value_change("support_material", false);
        }
    }

    if (config->opt_bool("wipe_tower") && config->opt_bool("support_material") &&
        config->opt_float("support_material_contact_distance") > 0. &&
        (config->opt_int("support_material_extruder") != 0 || config->opt_int("support_material_interface_extruder") != 0)) {
        wxString msg_text = _(L("The Wipe Tower currently supports the non-soluble supports only\n"
                                "if they are printed with the current extruder without triggering a tool change.\n"
                                "(both support_material_extruder and support_material_interface_extruder need to be set to 0)."));
        if (is_global_config)
            msg_text += "\n\n" + _(L("Shall I adjust those settings in order to enable the Wipe Tower?"));
        //wxMessageDialog dialog (nullptr, msg_text, _(L("Wipe Tower")),
        MessageDialog dialog (nullptr, msg_text, _(L("Wipe Tower")),
                                wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
        DynamicPrintConfig new_conf = *config;
        auto answer = dialog.ShowModal();
        if (!is_global_config || answer == wxID_YES) {
            new_conf.set_key_value("support_material_extruder", new ConfigOptionInt(0));
            new_conf.set_key_value("support_material_interface_extruder", new ConfigOptionInt(0));
        }
        else
            new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
        apply(config, &new_conf);
    }

    if (config->opt_bool("wipe_tower") && config->opt_bool("support_material") &&
        config->opt_float("support_material_contact_distance") == 0 &&
        !config->opt_bool("support_material_synchronize_layers")) {
        wxString msg_text = _(L("For the Wipe Tower to work with the soluble supports, the support layers\n"
                                "need to be synchronized with the object layers."));
        if (is_global_config)
            msg_text += "\n\n" + _(L("Shall I synchronize support layers in order to enable the Wipe Tower?"));
        //wxMessageDialog dialog(nullptr, msg_text, _(L("Wipe Tower")),
        MessageDialog dialog(nullptr, msg_text, _(L("Wipe Tower")),
                               wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
        DynamicPrintConfig new_conf = *config;
        auto answer = dialog.ShowModal();
        if (!is_global_config || answer == wxID_YES) {
            new_conf.set_key_value("support_material_synchronize_layers", new ConfigOptionBool(true));
        }
        else
            new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
        apply(config, &new_conf);
    }

    if (config->opt_bool("support_material")) {
        // Ask only once.
        if (!m_support_material_overhangs_queried) {
            m_support_material_overhangs_queried = true;
            if (!config->opt_bool("overhangs")/* != 1*/) {
                wxString msg_text = _(L("Supports work better, if the following feature is enabled:\n"
                                        "- Detect bridging perimeters"));
                if (is_global_config)
                    msg_text += "\n\n" + _(L("Shall I adjust those settings for supports?"));
                MessageDialog dialog(nullptr, msg_text, _L("Support Generator"), wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (!is_global_config || answer == wxID_YES) {
                    // Enable "detect bridging perimeters".
                    new_conf.set_key_value("overhangs", new ConfigOptionBool(true));
                }
                //else Do nothing, leave supports on and "detect bridging perimeters" off.
                apply(config, &new_conf);
            }
        }
    }
    else {
        m_support_material_overhangs_queried = false;
    }

    if (config->option<ConfigOptionPercent>("fill_density")->value == 100) {
        std::string  fill_pattern            = config->option<ConfigOptionEnum<InfillPattern>>("fill_pattern")->serialize();
        const auto  &top_fill_pattern_values = config->def()->get("top_fill_pattern")->enum_values;
        bool correct_100p_fill = std::find(top_fill_pattern_values.begin(), top_fill_pattern_values.end(), fill_pattern) != top_fill_pattern_values.end();
        if (!correct_100p_fill) {
            // get fill_pattern name from enum_labels for using this one at dialog_msg
            const ConfigOptionDef *fill_pattern_def = config->def()->get("fill_pattern");
            assert(fill_pattern_def != nullptr);
            auto it_pattern = std::find(fill_pattern_def->enum_values.begin(), fill_pattern_def->enum_values.end(), fill_pattern);
            assert(it_pattern != fill_pattern_def->enum_values.end());
            if (it_pattern != fill_pattern_def->enum_values.end()) {
                wxString msg_text = GUI::format_wxstr(_L("The %1% infill pattern is not supposed to work at 100%% density."), 
                    _(fill_pattern_def->enum_labels[it_pattern - fill_pattern_def->enum_values.begin()]));
                if (is_global_config)
                    msg_text += "\n\n" + _L("Shall I switch to rectilinear fill pattern?");
                //wxMessageDialog dialog(nullptr, msg_text, _L("Infill"),
                MessageDialog dialog(nullptr, msg_text, _L("Infill"),
                                                  wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK) );
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (!is_global_config || answer == wxID_YES) {
                    new_conf.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
                    fill_density = 100;
                }
                else
                    fill_density = wxGetApp().preset_bundle->prints.get_selected_preset().config.option<ConfigOptionPercent>("fill_density")->value;
                new_conf.set_key_value("fill_density", new ConfigOptionPercent(fill_density));
                apply(config, &new_conf);
                if (cb_value_change)
                    cb_value_change("fill_density", fill_density);
            }
        }
    }
}

void ConfigManipulation::toggle_print_fff_options(DynamicPrintConfig* config)
{
    bool have_perimeters = config->opt_int("perimeters") > 0;
    for (auto el : { "extra_perimeters", "ensure_vertical_shell_thickness", "thin_walls", "overhangs",
                    "seam_position", "external_perimeters_first", "external_perimeter_extrusion_width",
                    "perimeter_speed", "small_perimeter_speed", "external_perimeter_speed" })
        toggle_field(el, have_perimeters);

    bool have_infill = config->option<ConfigOptionPercent>("fill_density")->value > 0;
    // infill_extruder uses the same logic as in Print::extruders()
    for (auto el : { "fill_pattern", "infill_every_layers", "infill_only_where_needed",
                    "solid_infill_every_layers", "solid_infill_below_area", "infill_extruder", "infill_anchor_max" })
        toggle_field(el, have_infill);
    // Only allow configuration of open anchors if the anchoring is enabled.
    bool has_infill_anchors = have_infill && config->option<ConfigOptionFloatOrPercent>("infill_anchor_max")->value > 0;
    toggle_field("infill_anchor", has_infill_anchors);

    bool has_spiral_vase         = config->opt_bool("spiral_vase");
    bool has_top_solid_infill 	 = config->opt_int("top_solid_layers") > 0;
    bool has_bottom_solid_infill = config->opt_int("bottom_solid_layers") > 0;
    bool has_solid_infill 		 = has_top_solid_infill || has_bottom_solid_infill;
    // solid_infill_extruder uses the same logic as in Print::extruders()
    for (auto el : { "top_fill_pattern", "bottom_fill_pattern", "infill_first", "solid_infill_extruder",
                    "solid_infill_extrusion_width", "solid_infill_speed" })
        toggle_field(el, has_solid_infill);

    for (auto el : { "fill_angle", "bridge_angle", "infill_extrusion_width",
                    "infill_speed", "bridge_speed" })
        toggle_field(el, have_infill || has_solid_infill);

    toggle_field("top_solid_min_thickness", ! has_spiral_vase && has_top_solid_infill);
    toggle_field("bottom_solid_min_thickness", ! has_spiral_vase && has_bottom_solid_infill);

    // Gap fill is newly allowed in between perimeter lines even for empty infill (see GH #1476).
    toggle_field("gap_fill_speed", have_perimeters);

    for (auto el : { "top_infill_extrusion_width", "top_solid_infill_speed" })
        toggle_field(el, has_top_solid_infill || (has_spiral_vase && has_bottom_solid_infill));

    bool have_default_acceleration = config->opt_float("default_acceleration") > 0;
    for (auto el : { "perimeter_acceleration", "infill_acceleration",
                    "bridge_acceleration", "first_layer_acceleration" })
        toggle_field(el, have_default_acceleration);

    bool have_skirt = config->opt_int("skirts") > 0;
    toggle_field("skirt_height", have_skirt && config->opt_enum<DraftShield>("draft_shield") != dsEnabled);
    for (auto el : { "skirt_distance", "draft_shield", "min_skirt_length" })
        toggle_field(el, have_skirt);

    bool have_brim = config->opt_enum<BrimType>("brim_type") != btNoBrim;
    for (auto el : { "brim_width", "brim_separation" })
        toggle_field(el, have_brim);
    // perimeter_extruder uses the same logic as in Print::extruders()
    toggle_field("perimeter_extruder", have_perimeters || have_brim);

    bool have_raft = config->opt_int("raft_layers") > 0;
    bool have_support_material = config->opt_bool("support_material") || have_raft;
    bool have_support_material_auto = have_support_material && config->opt_bool("support_material_auto");
    bool have_support_interface = config->opt_int("support_material_interface_layers") > 0;
    bool have_support_soluble = have_support_material && config->opt_float("support_material_contact_distance") == 0;
    auto support_material_style = config->opt_enum<SupportMaterialStyle>("support_material_style");
    for (auto el : { "support_material_style", "support_material_pattern", "support_material_with_sheath",
                    "support_material_spacing", "support_material_angle", 
                    "support_material_interface_pattern", "support_material_interface_layers",
                    "dont_support_bridges", "support_material_extrusion_width", "support_material_contact_distance",
                    "support_material_xy_spacing" })
        toggle_field(el, have_support_material);
    toggle_field("support_material_threshold", have_support_material_auto);
    toggle_field("support_material_bottom_contact_distance", have_support_material && ! have_support_soluble);
    toggle_field("support_material_closing_radius", have_support_material && support_material_style == smsSnug);

    for (auto el : { "support_material_bottom_interface_layers", "support_material_interface_spacing", "support_material_interface_extruder",
                    "support_material_interface_speed", "support_material_interface_contact_loops" })
        toggle_field(el, have_support_material && have_support_interface);
    toggle_field("support_material_synchronize_layers", have_support_soluble);

    toggle_field("perimeter_extrusion_width", have_perimeters || have_skirt || have_brim);
    toggle_field("support_material_extruder", have_support_material || have_skirt);
    toggle_field("support_material_speed", have_support_material || have_brim || have_skirt);

    toggle_field("raft_contact_distance", have_raft && !have_support_soluble);
    for (auto el : { "raft_expansion", "first_layer_acceleration_over_raft", "first_layer_speed_over_raft" })
        toggle_field(el, have_raft);

    bool has_ironing = config->opt_bool("ironing");
    for (auto el : { "ironing_type", "ironing_flowrate", "ironing_spacing", "ironing_speed" })
    	toggle_field(el, has_ironing);

    bool have_sequential_printing = config->opt_bool("complete_objects");
    for (auto el : { "extruder_clearance_radius", "extruder_clearance_height" })
        toggle_field(el, have_sequential_printing);

    bool have_ooze_prevention = config->opt_bool("ooze_prevention");
    toggle_field("standby_temperature_delta", have_ooze_prevention);

    bool have_wipe_tower = config->opt_bool("wipe_tower");
    for (auto el : { "wipe_tower_x", "wipe_tower_y", "wipe_tower_width", "wipe_tower_rotation_angle", "wipe_tower_brim_width",
                     "wipe_tower_bridging", "wipe_tower_no_sparse_layers", "single_extruder_multi_material_priming" })
        toggle_field(el, have_wipe_tower);

    bool have_avoid_crossing_perimeters = config->opt_bool("avoid_crossing_perimeters");
    toggle_field("avoid_crossing_perimeters_max_detour", have_avoid_crossing_perimeters);
}

void ConfigManipulation::update_print_sla_config(DynamicPrintConfig* config, const bool is_global_config/* = false*/)
{
    double head_penetration = config->opt_float("support_head_penetration");
    double head_width = config->opt_float("support_head_width");
    if (head_penetration > head_width) {
        wxString msg_text = _(L("Head penetration should not be greater than the head width."));

        //wxMessageDialog dialog(nullptr, msg_text, _(L("Invalid Head penetration")), wxICON_WARNING | wxOK);
        MessageDialog dialog(nullptr, msg_text, _(L("Invalid Head penetration")), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        if (dialog.ShowModal() == wxID_OK) {
            new_conf.set_key_value("support_head_penetration", new ConfigOptionFloat(head_width));
            apply(config, &new_conf);
        }
    }

    double pinhead_d = config->opt_float("support_head_front_diameter");
    double pillar_d = config->opt_float("support_pillar_diameter");
    if (pinhead_d > pillar_d) {
        wxString msg_text = _(L("Pinhead diameter should be smaller than the pillar diameter."));

        //wxMessageDialog dialog(nullptr, msg_text, _(L("Invalid pinhead diameter")), wxICON_WARNING | wxOK);
        MessageDialog dialog(nullptr, msg_text, _(L("Invalid pinhead diameter")), wxICON_WARNING | wxOK);

        DynamicPrintConfig new_conf = *config;
        if (dialog.ShowModal() == wxID_OK) {
            new_conf.set_key_value("support_head_front_diameter", new ConfigOptionFloat(pillar_d / 2.0));
            apply(config, &new_conf);
        }
    }
}

void ConfigManipulation::toggle_print_sla_options(DynamicPrintConfig* config)
{
    bool supports_en = config->opt_bool("supports_enable");

    toggle_field("support_head_front_diameter", supports_en);
    toggle_field("support_head_penetration", supports_en);
    toggle_field("support_head_width", supports_en);
    toggle_field("support_pillar_diameter", supports_en);
    toggle_field("support_small_pillar_diameter_percent", supports_en);
    toggle_field("support_max_bridges_on_pillar", supports_en);
    toggle_field("support_pillar_connection_mode", supports_en);
    toggle_field("support_buildplate_only", supports_en);
    toggle_field("support_base_diameter", supports_en);
    toggle_field("support_base_height", supports_en);
    toggle_field("support_base_safety_distance", supports_en);
    toggle_field("support_critical_angle", supports_en);
    toggle_field("support_max_bridge_length", supports_en);
    toggle_field("support_max_pillar_link_distance", supports_en);
    toggle_field("support_points_density_relative", supports_en);
    toggle_field("support_points_minimal_distance", supports_en);

    bool pad_en = config->opt_bool("pad_enable");

    toggle_field("pad_wall_thickness", pad_en);
    toggle_field("pad_wall_height", pad_en);
    toggle_field("pad_brim_size", pad_en);
    toggle_field("pad_max_merge_distance", pad_en);
 // toggle_field("pad_edge_radius", supports_en);
    toggle_field("pad_wall_slope", pad_en);
    toggle_field("pad_around_object", pad_en);
    toggle_field("pad_around_object_everywhere", pad_en);

    bool zero_elev = config->opt_bool("pad_around_object") && pad_en;

    toggle_field("support_object_elevation", supports_en && !zero_elev);
    toggle_field("pad_object_gap", zero_elev);
    toggle_field("pad_around_object_everywhere", zero_elev);
    toggle_field("pad_object_connector_stride", zero_elev);
    toggle_field("pad_object_connector_width", zero_elev);
    toggle_field("pad_object_connector_penetration", zero_elev);
}


} // GUI
} // Slic3r
