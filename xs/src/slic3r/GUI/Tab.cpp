#include "../../libslic3r/GCodeSender.hpp"
#include "Tab.hpp"
#include "PresetBundle.hpp"
#include "PresetHints.hpp"
#include "../../libslic3r/Utils.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>

#include <boost/algorithm/string/predicate.hpp>

namespace Slic3r {
namespace GUI {

// sub new
void Tab::create_preset_tab(PresetBundle *preset_bundle)
{
	m_preset_bundle = preset_bundle;

	// Vertical sizer to hold the choice menu and the rest of the page.
	Tab *panel = this;
	auto  *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->SetSizeHints(panel);
	panel->SetSizer(sizer);

	// preset chooser
	m_presets_choice = new wxBitmapComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(270, -1), 0, 0,wxCB_READONLY);
	const wxBitmap* bmp = new wxBitmap(wxString::FromUTF8(Slic3r::var("flag-green-icon.png").c_str()), wxBITMAP_TYPE_PNG);

	//buttons
	wxBitmap bmpMenu;
	bmpMenu = wxBitmap(wxString::FromUTF8(Slic3r::var("disk.png").c_str()), wxBITMAP_TYPE_PNG);
	m_btn_save_preset = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	bmpMenu = wxBitmap(wxString::FromUTF8(Slic3r::var("delete.png").c_str()), wxBITMAP_TYPE_PNG);
	m_btn_delete_preset = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	m_show_incompatible_presets = false;
	m_bmp_show_incompatible_presets = new wxBitmap(wxString::FromUTF8(Slic3r::var("flag-red-icon.png").c_str()), wxBITMAP_TYPE_PNG);
	m_bmp_hide_incompatible_presets = new wxBitmap(wxString::FromUTF8(Slic3r::var("flag-green-icon.png").c_str()), wxBITMAP_TYPE_PNG);
	m_btn_hide_incompatible_presets = new wxBitmapButton(panel, wxID_ANY, *m_bmp_hide_incompatible_presets, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	m_btn_save_preset->SetToolTip(_L("Save current ") + m_title);
	m_btn_delete_preset->SetToolTip(_L("Delete this preset"));
	m_btn_delete_preset->Disable();

	m_hsizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_hsizer, 0, wxBOTTOM, 3);
	m_hsizer->Add(m_presets_choice, 1, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
	m_hsizer->AddSpacer(4);
	m_hsizer->Add(m_btn_save_preset, 0, wxALIGN_CENTER_VERTICAL);
	m_hsizer->AddSpacer(4);
	m_hsizer->Add(m_btn_delete_preset, 0, wxALIGN_CENTER_VERTICAL);
	m_hsizer->AddSpacer(16);
	m_hsizer->Add(m_btn_hide_incompatible_presets, 0, wxALIGN_CENTER_VERTICAL);

	//Horizontal sizer to hold the tree and the selected page.
	m_hsizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_hsizer, 1, wxEXPAND, 0);

	//left vertical sizer
	m_left_sizer = new wxBoxSizer(wxVERTICAL);
	m_hsizer->Add(m_left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);

	// tree
	m_treectrl = new wxTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(185, -1), 
		wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN | wxWANTS_CHARS);
	m_left_sizer->Add(m_treectrl, 1, wxEXPAND);
	m_icons = new wxImageList(16, 16, true, 1);
	// Index of the last icon inserted into $self->{icons}.
	m_icon_count = -1;
	m_treectrl->AssignImageList(m_icons);
	m_treectrl->AddRoot("root");
	m_treectrl->SetIndent(0);
	m_disable_tree_sel_changed_event = 0;

	m_treectrl->Bind(wxEVT_TREE_SEL_CHANGED, &Tab::OnTreeSelChange, this);
	m_treectrl->Bind(wxEVT_KEY_DOWN, &Tab::OnKeyDown, this);

	m_presets_choice->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent e){
		//! Because of The MSW and GTK version of wxBitmapComboBox derived from wxComboBox, 
		//! but the OSX version derived from wxOwnerDrawnCombo, instead of:
		//! select_preset(m_presets_choice->GetStringSelection().ToStdString()); 
		//! we doing next:
		int selected_item = m_presets_choice->GetSelection();
		if (selected_item >= 0){
			std::string selected_string = m_presets_choice->GetString(selected_item).ToUTF8().data();
			select_preset(selected_string);
		}
	}));

	m_btn_save_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e){ save_preset(); }));
	m_btn_delete_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e){ delete_preset(); }));
	m_btn_hide_incompatible_presets->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e){
		toggle_show_hide_incompatible();
	}));

	// Initialize the DynamicPrintConfig by default keys/values.
	build();
	rebuild_page_tree();
	update();
}

PageShp Tab::add_options_page(wxString title, std::string icon, bool is_extruder_pages/* = false*/)
{
	// Index of icon in an icon list $self->{icons}.
	auto icon_idx = 0;
	if (!icon.empty()) {
		try { icon_idx = m_icon_index.at(icon);}
		catch (std::out_of_range e) { icon_idx = -1; }
		if (icon_idx == -1) {
			// Add a new icon to the icon list.
			const auto img_icon = new wxIcon(wxString::FromUTF8(Slic3r::var(icon).c_str()), wxBITMAP_TYPE_PNG);
			m_icons->Add(*img_icon);
			icon_idx = ++m_icon_count;
			m_icon_index[icon] = icon_idx;
		}
	}
	// Initialize the page.
	PageShp page(new Page(this, title, icon_idx));
	page->SetScrollbars(1, 1, 1, 1);
	page->Hide();
	m_hsizer->Add(page.get(), 1, wxEXPAND | wxLEFT, 5);
	if (!is_extruder_pages) 
		m_pages.push_back(page);

	page->set_config(m_config);
	return page;
}

// Update the combo box label of the selected preset based on its "dirty" state,
// comparing the selected preset config with $self->{config}.
void Tab::update_dirty(){
	m_presets->update_dirty_ui(m_presets_choice);
	on_presets_changed();
}

void Tab::update_tab_ui()
{
	m_presets->update_tab_ui(m_presets_choice, m_show_incompatible_presets);
}

// Load a provied DynamicConfig into the tab, modifying the active preset.
// This could be used for example by setting a Wipe Tower position by interactive manipulation in the 3D view.
void Tab::load_config(DynamicPrintConfig config)
{
	bool modified = 0;
	boost::any value;
	for(auto opt_key : m_config->diff(config)) {
		switch ( config.def()->get(opt_key)->type ){
		case coFloatOrPercent:
			value = config.option<ConfigOptionFloatOrPercent>(opt_key)->value;
			break;
		case coPercent:
			value = config.option<ConfigOptionPercent>(opt_key)->value;
			break;
		case coFloat:
			value = config.opt_float(opt_key);
			break;
		case coString:
			value = config.opt_string(opt_key);
			break;
		case coPercents:
			value = config.option<ConfigOptionPercents>(opt_key)->values.at(0);
			break;
		case coFloats:
			value = config.opt_float(opt_key, 0);
			break;
		case coStrings:
			if (config.option<ConfigOptionStrings>(opt_key)->values.empty())
				value = "";
			else
				value = config.opt_string(opt_key, static_cast<unsigned int>(0));
			break;
		case coBool:
			value = config.opt_bool(opt_key);
			break;
		case coBools:
			value = config.opt_bool(opt_key, 0);
			break;
		case coInt:
			value = config.opt_int(opt_key);
			break;
		case coInts:
			value = config.opt_int(opt_key, 0);
			break;
		case coEnum:{
			if (opt_key.compare("external_fill_pattern") == 0 ||
				opt_key.compare("fill_pattern") == 0)
				value = config.option<ConfigOptionEnum<InfillPattern>>(opt_key)->value;
			else if (opt_key.compare("gcode_flavor") == 0)
				value = config.option<ConfigOptionEnum<GCodeFlavor>>(opt_key)->value;
			else if (opt_key.compare("support_material_pattern") == 0)
				value = config.option<ConfigOptionEnum<SupportMaterialPattern>>(opt_key)->value;
			else if (opt_key.compare("seam_position") == 0)
				value = config.option<ConfigOptionEnum<SeamPosition>>(opt_key)->value;
		}
			break;
		case coPoints:
			break;
		case coNone:
			break;
		default:
			break;
		}
		change_opt_value(*m_config, opt_key, value);
		modified = 1;
	}
	if (modified) {
		update_dirty();
		//# Initialize UI components with the config values.
		reload_config();
		update();
	}
}

// Reload current $self->{config} (aka $self->{presets}->edited_preset->config) into the UI fields.
void Tab::reload_config(){
	Freeze();
	for (auto page : m_pages)
		page->reload_config();
 	Thaw();
}

Field* Tab::get_field(t_config_option_key opt_key, int opt_index/* = -1*/) const
{
	Field* field = nullptr;
	for (auto page : m_pages){
		field = page->get_field(opt_key, opt_index);
		if (field != nullptr)
			return field;
	}
	return field;
}

// Set a key/value pair on this page. Return true if the value has been modified.
// Currently used for distributing extruders_count over preset pages of Slic3r::GUI::Tab::Printer
// after a preset is loaded.
bool Tab::set_value(t_config_option_key opt_key, boost::any value){
	bool changed = false;
	for(auto page: m_pages) {
		if (page->set_value(opt_key, value))
		changed = true;
	}
	return changed;
}

// To be called by custom widgets, load a value into a config,
// update the preset selection boxes (the dirty flags)
void Tab::load_key_value(std::string opt_key, boost::any value)
{
	change_opt_value(*m_config, opt_key, value);
	// Mark the print & filament enabled if they are compatible with the currently selected preset.
	if (opt_key.compare("compatible_printers") == 0) {
		m_preset_bundle->update_compatible_with_printer(0);
	} 
	m_presets->update_dirty_ui(m_presets_choice);
	on_presets_changed();
	update();
}

extern wxFrame *g_wxMainFrame;

void Tab::on_value_change(std::string opt_key, boost::any value)
{
	if (m_event_value_change > 0) {
		wxCommandEvent event(m_event_value_change);
		std::string str_out = opt_key + " " + m_name;
		event.SetString(str_out);
		if (opt_key == "extruders_count")
		{
			int val = boost::any_cast<size_t>(value);
			event.SetInt(val);
		}
		g_wxMainFrame->ProcessWindowEvent(event);
	}
	update();
}

// Call a callback to update the selection of presets on the platter:
// To update the content of the selection boxes,
// to update the filament colors of the selection boxes,
// to update the "dirty" flags of the selection boxes,
// to uddate number of "filament" selection boxes when the number of extruders change.
void Tab::on_presets_changed()
{
	if (m_event_presets_changed > 0) {
		wxCommandEvent event(m_event_presets_changed);
		event.SetString(m_name);
		g_wxMainFrame->ProcessWindowEvent(event);
	}
}

void Tab::reload_compatible_printers_widget()
{
	bool has_any = !m_config->option<ConfigOptionStrings>("compatible_printers")->values.empty();
	has_any ? m_compatible_printers_btn->Enable() : m_compatible_printers_btn->Disable();
	m_compatible_printers_checkbox->SetValue(!has_any);
	get_field("compatible_printers_condition")->toggle(!has_any);
}

void TabPrint::build()
{
	m_presets = &m_preset_bundle->prints;
	m_config = &m_presets->get_edited_preset().config;

	auto page = add_options_page(_L("Layers and perimeters"), "layers.png");
		auto optgroup = page->new_optgroup(_L("Layer height"));
		optgroup->append_single_option_line("layer_height");
		optgroup->append_single_option_line("first_layer_height");

		optgroup = page->new_optgroup(_L("Vertical shells"));
		optgroup->append_single_option_line("perimeters");
		optgroup->append_single_option_line("spiral_vase");

		Line line { "", "" };
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_recommended_thin_wall_thickness_description_line);
		};
		optgroup->append_line(line);

		optgroup = page->new_optgroup(_L("Horizontal shells"));
		line = { _L("Solid layers"), "" };
		line.append_option(optgroup->get_option("top_solid_layers"));
		line.append_option(optgroup->get_option("bottom_solid_layers"));
		optgroup->append_line(line);

		optgroup = page->new_optgroup(_L("Quality (slower slicing)"));
		optgroup->append_single_option_line("extra_perimeters");
		optgroup->append_single_option_line("ensure_vertical_shell_thickness");
		optgroup->append_single_option_line("avoid_crossing_perimeters");
		optgroup->append_single_option_line("thin_walls");
		optgroup->append_single_option_line("overhangs");

		optgroup = page->new_optgroup(_L("Advanced"));
		optgroup->append_single_option_line("seam_position");
		optgroup->append_single_option_line("external_perimeters_first");

	page = add_options_page(_L("Infill"), "infill.png");
		optgroup = page->new_optgroup(_L("Infill"));
		optgroup->append_single_option_line("fill_density");
		optgroup->append_single_option_line("fill_pattern");
		optgroup->append_single_option_line("external_fill_pattern");

		optgroup = page->new_optgroup(_L("Reducing printing time"));
		optgroup->append_single_option_line("infill_every_layers");
		optgroup->append_single_option_line("infill_only_where_needed");

		optgroup = page->new_optgroup(_L("Advanced"));
		optgroup->append_single_option_line("solid_infill_every_layers");
		optgroup->append_single_option_line("fill_angle");
		optgroup->append_single_option_line("solid_infill_below_area");
		optgroup->append_single_option_line("bridge_angle");
		optgroup->append_single_option_line("only_retract_when_crossing_perimeters");
		optgroup->append_single_option_line("infill_first");

	page = add_options_page(_L("Skirt and brim"), "box.png");
		optgroup = page->new_optgroup(_L("Skirt"));
		optgroup->append_single_option_line("skirts");
		optgroup->append_single_option_line("skirt_distance");
		optgroup->append_single_option_line("skirt_height");
		optgroup->append_single_option_line("min_skirt_length");

		optgroup = page->new_optgroup(_L("Brim"));
		optgroup->append_single_option_line("brim_width");

	page = add_options_page(_L("Support material"), "building.png");
		optgroup = page->new_optgroup(_L("Support material"));
		optgroup->append_single_option_line("support_material");
		optgroup->append_single_option_line("support_material_threshold");
		optgroup->append_single_option_line("support_material_enforce_layers");

		optgroup = page->new_optgroup(_L("Raft"));
		optgroup->append_single_option_line("raft_layers");
//		# optgroup->append_single_option_line(get_option_("raft_contact_distance");

		optgroup = page->new_optgroup(_L("Options for support material and raft"));
		optgroup->append_single_option_line("support_material_contact_distance");
		optgroup->append_single_option_line("support_material_pattern");
		optgroup->append_single_option_line("support_material_with_sheath");
		optgroup->append_single_option_line("support_material_spacing");
		optgroup->append_single_option_line("support_material_angle");
		optgroup->append_single_option_line("support_material_interface_layers");
		optgroup->append_single_option_line("support_material_interface_spacing");
		optgroup->append_single_option_line("support_material_interface_contact_loops");
		optgroup->append_single_option_line("support_material_buildplate_only");
		optgroup->append_single_option_line("support_material_xy_spacing");
		optgroup->append_single_option_line("dont_support_bridges");
		optgroup->append_single_option_line("support_material_synchronize_layers");

	page = add_options_page(_L("Speed"), "time.png");
		optgroup = page->new_optgroup(_L("Speed for print moves"));
		optgroup->append_single_option_line("perimeter_speed");
		optgroup->append_single_option_line("small_perimeter_speed");
		optgroup->append_single_option_line("external_perimeter_speed");
		optgroup->append_single_option_line("infill_speed");
		optgroup->append_single_option_line("solid_infill_speed");
		optgroup->append_single_option_line("top_solid_infill_speed");
		optgroup->append_single_option_line("support_material_speed");
		optgroup->append_single_option_line("support_material_interface_speed");
		optgroup->append_single_option_line("bridge_speed");
		optgroup->append_single_option_line("gap_fill_speed");

		optgroup = page->new_optgroup(_L("Speed for non-print moves"));
		optgroup->append_single_option_line("travel_speed");

		optgroup = page->new_optgroup(_L("Modifiers"));
		optgroup->append_single_option_line("first_layer_speed");

		optgroup = page->new_optgroup(_L("Acceleration control (advanced)"));
		optgroup->append_single_option_line("perimeter_acceleration");
		optgroup->append_single_option_line("infill_acceleration");
		optgroup->append_single_option_line("bridge_acceleration");
		optgroup->append_single_option_line("first_layer_acceleration");
		optgroup->append_single_option_line("default_acceleration");

		optgroup = page->new_optgroup(_L("Autospeed (advanced)"));
		optgroup->append_single_option_line("max_print_speed");
		optgroup->append_single_option_line("max_volumetric_speed");
		optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_positive");
		optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_negative");

	page = add_options_page(_L("Multiple Extruders"), "funnel.png");
		optgroup = page->new_optgroup(_L("Extruders"));
		optgroup->append_single_option_line("perimeter_extruder");
		optgroup->append_single_option_line("infill_extruder");
		optgroup->append_single_option_line("solid_infill_extruder");
		optgroup->append_single_option_line("support_material_extruder");
		optgroup->append_single_option_line("support_material_interface_extruder");

		optgroup = page->new_optgroup(_L("Ooze prevention"));
		optgroup->append_single_option_line("ooze_prevention");
		optgroup->append_single_option_line("standby_temperature_delta");

		optgroup = page->new_optgroup(_L("Wipe tower"));
		optgroup->append_single_option_line("wipe_tower");
		optgroup->append_single_option_line("wipe_tower_x");
		optgroup->append_single_option_line("wipe_tower_y");
		optgroup->append_single_option_line("wipe_tower_width");
		optgroup->append_single_option_line("wipe_tower_per_color_wipe");

		optgroup = page->new_optgroup(_L("Advanced"));
		optgroup->append_single_option_line("interface_shells");

	page = add_options_page(_L("Advanced"), "wrench.png");
		optgroup = page->new_optgroup(_L("Extrusion width"), 180);
		optgroup->append_single_option_line("extrusion_width");
		optgroup->append_single_option_line("first_layer_extrusion_width");
		optgroup->append_single_option_line("perimeter_extrusion_width");
		optgroup->append_single_option_line("external_perimeter_extrusion_width");
		optgroup->append_single_option_line("infill_extrusion_width");
		optgroup->append_single_option_line("solid_infill_extrusion_width");
		optgroup->append_single_option_line("top_infill_extrusion_width");
		optgroup->append_single_option_line("support_material_extrusion_width");

		optgroup = page->new_optgroup(_L("Overlap"));
		optgroup->append_single_option_line("infill_overlap");

		optgroup = page->new_optgroup(_L("Flow"));
		optgroup->append_single_option_line("bridge_flow_ratio");

		optgroup = page->new_optgroup(_L("Other"));
		optgroup->append_single_option_line("clip_multipart_objects");
		optgroup->append_single_option_line("elefant_foot_compensation");
		optgroup->append_single_option_line("xy_size_compensation");
//		#            optgroup->append_single_option_line("threads");
		optgroup->append_single_option_line("resolution");

	page = add_options_page(_L("Output options"), "page_white_go.png");
		optgroup = page->new_optgroup(_L("Sequential printing"));
		optgroup->append_single_option_line("complete_objects");
		line = { _L("Extruder clearance (mm)"), "" };
		Option option = optgroup->get_option("extruder_clearance_radius");
		option.opt.width = 60;
		line.append_option(option);
		option = optgroup->get_option("extruder_clearance_height");
		option.opt.width = 60;
		line.append_option(option);
		optgroup->append_line(line);

		optgroup = page->new_optgroup(_L("Output file"));
		optgroup->append_single_option_line("gcode_comments");
		option = optgroup->get_option("output_filename_format");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_L("Post-processing scripts"), 0);	
		option = optgroup->get_option("post_process");
		option.opt.full_width = true;
		option.opt.height = 50;
		optgroup->append_single_option_line(option);

	page = add_options_page(_L("Notes"), "note.png");
		optgroup = page->new_optgroup(_L("Notes"), 0);						
		option = optgroup->get_option("notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page(_L("Dependencies"), "wrench.png");
		optgroup = page->new_optgroup(_L("Profile dependencies"));
		line = { _L("Compatible printers"), "" };
		line.widget = [this](wxWindow* parent){
			return compatible_printers_widget(parent, &m_compatible_printers_checkbox, &m_compatible_printers_btn);
		};
		optgroup->append_line(line);

		option = optgroup->get_option("compatible_printers_condition");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabPrint::reload_config(){
	reload_compatible_printers_widget();
	Tab::reload_config();
}

void TabPrint::update()
{
	Freeze();

	if ( m_config->opt_bool("spiral_vase") && 
		!(m_config->opt_int("perimeters") == 1 && m_config->opt_int("top_solid_layers") == 0 &&
			m_config->option<ConfigOptionPercent>("fill_density")->value == 0)) {
		wxString msg_text = _L("The Spiral Vase mode requires:\n"
			"- one perimeter\n"
 			"- no top solid layers\n"
 			"- 0% fill density\n"
 			"- no support material\n"
 			"- no ensure_vertical_shell_thickness\n"
  			"\nShall I adjust those settings in order to enable Spiral Vase?");
		auto dialog = new wxMessageDialog(parent(), msg_text, _L("Spiral Vase"), wxICON_WARNING | wxYES | wxNO);
		DynamicPrintConfig new_conf = *m_config;
 		if (dialog->ShowModal() == wxID_YES) {
			new_conf.set_key_value("perimeters", new ConfigOptionInt(1));
			new_conf.set_key_value("top_solid_layers", new ConfigOptionInt(0));
			new_conf.set_key_value("fill_density", new ConfigOptionPercent(0));
			new_conf.set_key_value("support_material", new ConfigOptionBool(false));
			new_conf.set_key_value("ensure_vertical_shell_thickness", new ConfigOptionBool(false));
		}
		else {
			new_conf.set_key_value("spiral_vase", new ConfigOptionBool(false));
		}
 		load_config(new_conf);
	}

	auto first_layer_height = m_config->option<ConfigOptionFloatOrPercent>("first_layer_height")->value;
	auto layer_height = m_config->opt_float("layer_height");
	if (m_config->opt_bool("wipe_tower") &&
		( first_layer_height != 0.2 || layer_height < 0.15 || layer_height > 0.35)) {
		wxString msg_text = _L("The Wipe Tower currently supports only:\n"
			"- first layer height 0.2mm\n"
			"- layer height from 0.15mm to 0.35mm\n"
			"\nShall I adjust those settings in order to enable the Wipe Tower?");
		auto dialog = new wxMessageDialog(parent(), msg_text, _L("Wipe Tower"), wxICON_WARNING | wxYES | wxNO);
		DynamicPrintConfig new_conf = *m_config;
		if (dialog->ShowModal() == wxID_YES) {
			const auto &val = *m_config->option<ConfigOptionFloatOrPercent>("first_layer_height");
			new_conf.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(0.2, val.percent));

			if (m_config->opt_float("layer_height") < 0.15) new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.15)) ;
			if (m_config->opt_float("layer_height") > 0.35) new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.35));
		}
		else 
			new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
		load_config(new_conf);
	}

	if (m_config->opt_bool("wipe_tower") && m_config->opt_bool("support_material") && 
		m_config->opt_float("support_material_contact_distance") > 0. &&
		(m_config->opt_int("support_material_extruder") != 0 || m_config->opt_int("support_material_interface_extruder") != 0)) {
		wxString msg_text = _L("The Wipe Tower currently supports the non-soluble supports only\n"
			"if they are printed with the current extruder without triggering a tool change.\n"
			"(both support_material_extruder and support_material_interface_extruder need to be set to 0).\n"
			"\nShall I adjust those settings in order to enable the Wipe Tower?");
		auto dialog = new wxMessageDialog(parent(), msg_text, _L("Wipe Tower"), wxICON_WARNING | wxYES | wxNO);
		DynamicPrintConfig new_conf = *m_config;
		if (dialog->ShowModal() == wxID_YES) {
			new_conf.set_key_value("support_material_extruder", new ConfigOptionInt(0));
			new_conf.set_key_value("support_material_interface_extruder", new ConfigOptionInt(0));
		}
		else 
			new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
		load_config(new_conf);
	}

	if (m_config->opt_bool("wipe_tower") && m_config->opt_bool("support_material") && 
		m_config->opt_float("support_material_contact_distance") == 0 &&
		!m_config->opt_bool("support_material_synchronize_layers")) {
		wxString msg_text = _L("For the Wipe Tower to work with the soluble supports, the support layers\n"
			"need to be synchronized with the object layers.\n"
			"\nShall I synchronize support layers in order to enable the Wipe Tower?");
		auto dialog = new wxMessageDialog(parent(), msg_text, _L("Wipe Tower"), wxICON_WARNING | wxYES | wxNO);
		DynamicPrintConfig new_conf = *m_config;
		if (dialog->ShowModal() == wxID_YES) {
			new_conf.set_key_value("support_material_synchronize_layers", new ConfigOptionBool(true));
		}
		else
			new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
		load_config(new_conf);
	}

	if (m_config->opt_bool("support_material")) {
		// Ask only once.
		if (!m_support_material_overhangs_queried) {
			m_support_material_overhangs_queried = true;
			if (!m_config->opt_bool("overhangs")/* != 1*/) {
				wxString msg_text = _L("Supports work better, if the following feature is enabled:\n"
					"- Detect bridging perimeters\n"
					"\nShall I adjust those settings for supports?");
				auto dialog = new wxMessageDialog(parent(), msg_text, _L("Support Generator"), wxICON_WARNING | wxYES | wxNO | wxCANCEL);
				DynamicPrintConfig new_conf = *m_config;
				auto answer = dialog->ShowModal();
				if (answer == wxID_YES) {
					// Enable "detect bridging perimeters".
					new_conf.set_key_value("overhangs", new ConfigOptionBool(true));
				} else if(answer == wxID_NO) {
					// Do nothing, leave supports on and "detect bridging perimeters" off.
				} else if(answer == wxID_CANCEL) {
					// Disable supports.
					new_conf.set_key_value("support_material", new ConfigOptionBool(false));
					m_support_material_overhangs_queried = false;
				}
				load_config(new_conf);
			}
		}
	}
	else {
		m_support_material_overhangs_queried = false;
	}

	if (m_config->option<ConfigOptionPercent>("fill_density")->value == 100) {
		auto fill_pattern = m_config->option<ConfigOptionEnum<InfillPattern>>("fill_pattern")->value;
		std::string str_fill_pattern = "";
		t_config_enum_values map_names = m_config->option<ConfigOptionEnum<InfillPattern>>("fill_pattern")->get_enum_values();
		for (auto it:map_names) {
			if (fill_pattern == it.second) {
				str_fill_pattern = it.first;
				break;
			}
		}
		if (!str_fill_pattern.empty()){
			auto external_fill_pattern = m_config->def()->get("external_fill_pattern")->enum_values;
			bool correct_100p_fill = false;
			for (auto fill : external_fill_pattern)
			{
				if (str_fill_pattern.compare(fill) == 0)
					correct_100p_fill = true;
			}
			// get fill_pattern name from enum_labels for using this one at dialog_msg
			str_fill_pattern = m_config->def()->get("fill_pattern")->enum_labels[fill_pattern];
			if (!correct_100p_fill){
				wxString msg_text = _L("The ") + str_fill_pattern + _L(" infill pattern is not supposed to work at 100% density.\n"
					"\nShall I switch to rectilinear fill pattern?");
				auto dialog = new wxMessageDialog(parent(), msg_text, _L("Infill"), wxICON_WARNING | wxYES | wxNO);
				DynamicPrintConfig new_conf = *m_config;
				if (dialog->ShowModal() == wxID_YES) {
					new_conf.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
					new_conf.set_key_value("fill_density", new ConfigOptionPercent(100));
				}
				else
					new_conf.set_key_value("fill_density", new ConfigOptionPercent(40));
				load_config(new_conf);
			}
		}
	}

	bool have_perimeters = m_config->opt_int("perimeters") > 0;
	std::vector<std::string> vec_enable = { "extra_perimeters", "ensure_vertical_shell_thickness", "thin_walls", "overhangs",
											"seam_position", "external_perimeters_first", "external_perimeter_extrusion_width",
											"perimeter_speed", "small_perimeter_speed", "external_perimeter_speed" };
	for (auto el : vec_enable)
		get_field(el)->toggle(have_perimeters);

	bool have_infill = m_config->option<ConfigOptionPercent>("fill_density")->value > 0;
	vec_enable.resize(0);
	vec_enable = {	"fill_pattern", "infill_every_layers", "infill_only_where_needed", 
					"solid_infill_every_layers", "solid_infill_below_area", "infill_extruder"};
	// infill_extruder uses the same logic as in Print::extruders()
	for (auto el : vec_enable)
		get_field(el)->toggle(have_infill);

	bool have_solid_infill = m_config->opt_int("top_solid_layers") > 0 || m_config->opt_int("bottom_solid_layers") > 0;
	vec_enable.resize(0);
	vec_enable = { "external_fill_pattern", "infill_first", "solid_infill_extruder",
		"solid_infill_extrusion_width", "solid_infill_speed" };
	// solid_infill_extruder uses the same logic as in Print::extruders()
	for (auto el : vec_enable)
		get_field(el)->toggle(have_solid_infill);

	vec_enable.resize(0);
	vec_enable = { "fill_angle", "bridge_angle", "infill_extrusion_width", 
					"infill_speed", "bridge_speed" };
	for (auto el : vec_enable)
		get_field(el)->toggle(have_infill || have_solid_infill);

	get_field("gap_fill_speed")->toggle(have_perimeters && have_infill);

	bool have_top_solid_infill = m_config->opt_int("top_solid_layers") > 0;
	vec_enable.resize(0);
	vec_enable = { "top_infill_extrusion_width", "top_solid_infill_speed" };
	for (auto el : vec_enable)
		get_field(el)->toggle(have_top_solid_infill);

	bool have_default_acceleration = m_config->opt_float("default_acceleration") > 0;
	vec_enable.resize(0);
	vec_enable = {	"perimeter_acceleration", "infill_acceleration", 
					"bridge_acceleration", "first_layer_acceleration"};
	for (auto el : vec_enable)
		get_field(el)->toggle(have_default_acceleration);

	bool have_skirt = m_config->opt_int("skirts") > 0 || m_config->opt_float("min_skirt_length") > 0;
	vec_enable.resize(0);
	vec_enable = {	"skirt_distance", "skirt_height"};
	for (auto el : vec_enable)
		get_field(el)->toggle(have_skirt);

	bool have_brim = m_config->opt_float("brim_width") > 0;
	// perimeter_extruder uses the same logic as in Print::extruders()
	get_field("perimeter_extruder")->toggle(have_perimeters || have_brim);

	bool have_raft = m_config->opt_int("raft_layers") > 0;
	bool have_support_material = m_config->opt_bool("support_material") || have_raft;
	bool have_support_interface = m_config->opt_int("support_material_interface_layers") > 0;
	bool have_support_soluble = have_support_material && m_config->opt_float("support_material_contact_distance") == 0;
	vec_enable.resize(0);
	vec_enable = {	"support_material_threshold", "support_material_pattern", "support_material_with_sheath",
					"support_material_spacing", "support_material_angle", "support_material_interface_layers", 
					"dont_support_bridges", "support_material_extrusion_width", "support_material_contact_distance", 
					"support_material_xy_spacing"};
	for (auto el : vec_enable)
		get_field(el)->toggle(have_support_material);

	vec_enable.resize(0);
	vec_enable = {	"support_material_interface_spacing", "support_material_interface_extruder", 
					"support_material_interface_speed", "support_material_interface_contact_loops"};
	for (auto el : vec_enable)
		get_field(el)->toggle(have_support_material && have_support_interface);
	get_field("support_material_synchronize_layers")->toggle(have_support_soluble);

	get_field("perimeter_extrusion_width")->toggle(have_perimeters || have_skirt || have_brim);
	get_field("support_material_extruder")->toggle(have_support_material || have_skirt);
	get_field("support_material_speed")->toggle(have_support_material || have_brim || have_skirt);

	bool have_sequential_printing = m_config->opt_bool("complete_objects");
	vec_enable.resize(0);
	vec_enable = {	"extruder_clearance_radius", "extruder_clearance_height"};
	for (auto el : vec_enable)
		get_field(el)->toggle(have_sequential_printing);

	bool have_ooze_prevention = m_config->opt_bool("ooze_prevention");
	get_field("standby_temperature_delta")->toggle(have_ooze_prevention);

	bool have_wipe_tower = m_config->opt_bool("wipe_tower");
	vec_enable.resize(0);
	vec_enable = {	"wipe_tower_x", "wipe_tower_y", "wipe_tower_width", "wipe_tower_per_color_wipe"};
	for (auto el : vec_enable)
		get_field(el)->toggle(have_wipe_tower);

	m_recommended_thin_wall_thickness_description_line->SetText(
		PresetHints::recommended_thin_wall_thickness(*m_preset_bundle));

	Thaw();
}

void TabPrint::OnActivate()
{
	m_recommended_thin_wall_thickness_description_line->SetText(PresetHints::recommended_thin_wall_thickness(*m_preset_bundle));
}

void TabFilament::build()
{
	m_presets = &m_preset_bundle->filaments;
	m_config = &m_preset_bundle->filaments.get_edited_preset().config;

	auto page = add_options_page(_L("Filament"), "spool.png");
		auto optgroup = page->new_optgroup(_L("Filament"));
		optgroup->append_single_option_line("filament_colour");
		optgroup->append_single_option_line("filament_diameter");
		optgroup->append_single_option_line("extrusion_multiplier");
		optgroup->append_single_option_line("filament_density");
		optgroup->append_single_option_line("filament_cost");

		optgroup = page->new_optgroup(_L("Temperature") +" (\u00B0C)"); // degree sign
		Line line = { _L("Extruder"), "" };
		line.append_option(optgroup->get_option("first_layer_temperature"));
		line.append_option(optgroup->get_option("temperature"));
		optgroup->append_line(line);

		line = { _L("Bed"), "" };
		line.append_option(optgroup->get_option("first_layer_bed_temperature"));
		line.append_option(optgroup->get_option("bed_temperature"));
		optgroup->append_line(line);

	page = add_options_page(_L("Cooling"), "hourglass.png");
		optgroup = page->new_optgroup(_L("Enable"));
		optgroup->append_single_option_line("fan_always_on");
		optgroup->append_single_option_line("cooling");

		line = { "", "" }; 
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_cooling_description_line);
		};
		optgroup->append_line(line);

		optgroup = page->new_optgroup(_L("Fan settings"));
		line = {_L("Fan speed"),""};
		line.append_option(optgroup->get_option("min_fan_speed"));
		line.append_option(optgroup->get_option("max_fan_speed"));
		optgroup->append_line(line);

		optgroup->append_single_option_line("bridge_fan_speed");
		optgroup->append_single_option_line("disable_fan_first_layers");

		optgroup = page->new_optgroup(_L("Cooling thresholds"), 250);
		optgroup->append_single_option_line("fan_below_layer_time");
		optgroup->append_single_option_line("slowdown_below_layer_time");
		optgroup->append_single_option_line("min_print_speed");

	page = add_options_page(_L("Advanced"), "wrench.png");
		optgroup = page->new_optgroup(_L("Filament properties"));
		optgroup->append_single_option_line("filament_type");
		optgroup->append_single_option_line("filament_soluble");

		optgroup = page->new_optgroup(_L("Print speed override"));
		optgroup->append_single_option_line("filament_max_volumetric_speed");

		line = {"",""};
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_volumetric_speed_description_line);
		};
		optgroup->append_line(line);

	page = add_options_page(_L("Custom G-code"), "cog.png");
		optgroup = page->new_optgroup(_L("Start G-code"), 0);
		Option option = optgroup->get_option("start_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_L("End G-code"), 0);
		option = optgroup->get_option("end_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

	page = add_options_page(_L("Notes"), "note.png");
		optgroup = page->new_optgroup(_L("Notes"), 0);
		optgroup->label_width = 0;
		option = optgroup->get_option("filament_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page(_L("Dependencies"), "wrench.png");
		optgroup = page->new_optgroup(_L("Profile dependencies"));
		line = {_L("Compatible printers"), ""};
		line.widget = [this](wxWindow* parent){
			return compatible_printers_widget(parent, &m_compatible_printers_checkbox, &m_compatible_printers_btn);
		};
		optgroup->append_line(line);

		option = optgroup->get_option("compatible_printers_condition");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabFilament::reload_config(){
	reload_compatible_printers_widget();
	Tab::reload_config();
}

void TabFilament::update()
{
	wxString text = wxString::FromUTF8(PresetHints::cooling_description(m_presets->get_edited_preset()).c_str());
	m_cooling_description_line->SetText(text);
	text = wxString::FromUTF8(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle).c_str());
	m_volumetric_speed_description_line->SetText(text);

	bool cooling = m_config->opt_bool("cooling", 0);
	bool fan_always_on = cooling || m_config->opt_bool("fan_always_on", 0);

	std::vector<std::string> vec_enable = { "max_fan_speed", "fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed" };
	for (auto el : vec_enable)
		get_field(el)->toggle(cooling);

	vec_enable.resize(0);
	vec_enable = { "min_fan_speed", "disable_fan_first_layers" };
	for (auto el : vec_enable)
		get_field(el)->toggle(fan_always_on);
}

void TabFilament::OnActivate()
{
	m_volumetric_speed_description_line->SetText(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle));
}

wxSizer* Tab::description_line_widget(wxWindow* parent, ogStaticText* *StaticText)
{
	*StaticText = new ogStaticText(parent, "");

	auto font = (new wxSystemSettings)->GetFont(wxSYS_DEFAULT_GUI_FONT);
	(*StaticText)->SetFont(font);

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(*StaticText);
	return sizer;
}

bool Tab::current_preset_is_dirty()
{
	return m_presets->current_is_dirty();
}

void TabPrinter::build()
{
	m_presets = &m_preset_bundle->printers;
	m_config = &m_preset_bundle->printers.get_edited_preset().config;
	auto default_config = m_preset_bundle->full_config();

	auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"));
	m_extruders_count = nozzle_diameter->values.size();

	auto page = add_options_page(_L("General"), "printer_empty.png");
		auto optgroup = page->new_optgroup(_L("Size and coordinates"));

		Line line{ _L("Bed shape"), "" };
		line.widget = [this](wxWindow* parent){
			auto btn = new wxButton(parent, wxID_ANY, _L("Set")+"\u2026", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
//			btn->SetFont(Slic3r::GUI::small_font);
			btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("printer_empty.png").c_str()), wxBITMAP_TYPE_PNG));

			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e)
			{
				auto dlg = new BedShapeDialog(this);
				dlg->build_dialog(m_config->option<ConfigOptionPoints>("bed_shape"));
				if (dlg->ShowModal() == wxID_OK)
					load_key_value("bed_shape", dlg->GetValue());
			}));

			return sizer;
		};
		optgroup->append_line(line);
		optgroup->append_single_option_line("z_offset");

		optgroup = page->new_optgroup(_L("Capabilities"));
		ConfigOptionDef def;
			def.type =  coInt,
			def.default_value = new ConfigOptionInt(1); 
			def.label = _LU8("Extruders");
			def.tooltip = _LU8("Number of extruders of the printer.");
			def.min = 1;
		Option option(def, "extruders_count");
		optgroup->append_single_option_line(option);
		optgroup->append_single_option_line("single_extruder_multi_material");

		optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value){
			size_t extruders_count = boost::any_cast<int>(optgroup->get_value("extruders_count"));
			wxTheApp->CallAfter([this, opt_key, value, extruders_count](){
				if (opt_key.compare("extruders_count")==0) {
					extruders_count_changed(extruders_count);
					update_dirty();
				}
				else {
					update_dirty();
					on_value_change(opt_key, value);
				}
			});
		};

		if (!m_no_controller)
		{
		optgroup = page->new_optgroup(_L("USB/Serial connection"));
			line = {_L("Serial port"), ""};
			Option serial_port = optgroup->get_option("serial_port");
			serial_port.side_widget = ([this](wxWindow* parent){
				auto btn = new wxBitmapButton(parent, wxID_ANY, wxBitmap(wxString::FromUTF8(Slic3r::var("arrow_rotate_clockwise.png").c_str()), wxBITMAP_TYPE_PNG),
					wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
				btn->SetToolTip(_L("Rescan serial ports"));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent e) {update_serial_ports(); });
				return sizer;
			});
			auto serial_test = [this](wxWindow* parent){
				auto btn = m_serial_test_btn = new wxButton(parent, wxID_ANY,
					_L("Test"), wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
//				btn->SetFont($Slic3r::GUI::small_font);
				btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("wrench.png").c_str()), wxBITMAP_TYPE_PNG));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent e){
					auto sender = new GCodeSender();					
					auto res = sender->connect(
						m_config->opt_string("serial_port"), 
						m_config->opt_int("serial_speed")
						);
					if (res && sender->wait_connected()) {
						show_info(parent, _L("Connection to printer works correctly."), _L("Success!"));
					}
					else {
						show_error(parent, _L("Connection failed."));
					}
				});
				return sizer;
			};

			line.append_option(serial_port);
			line.append_option(optgroup->get_option("serial_speed"));
			line.append_widget(serial_test);
			optgroup->append_line(line);
		}

		optgroup = page->new_optgroup(_L("OctoPrint upload"));
		// # append two buttons to the Host line
		auto octoprint_host_browse = [this] (wxWindow* parent) {
			auto btn = new wxButton(parent, wxID_ANY, _L("Browse")+"\u2026", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
//			btn->SetFont($Slic3r::GUI::small_font);
			btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("zoom.png").c_str()), wxBITMAP_TYPE_PNG));
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			if (m_is_disabled_button_browse) 
				btn->Disable();

			btn->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent e){
				if (m_event_button_browse > 0){
					wxCommandEvent event(m_event_button_browse);
					event.SetString(_L("Button BROWSE was clicked!"));
					g_wxMainFrame->ProcessWindowEvent(event);
				}
// 				// # look for devices
// 				auto entries;
// 				{
// 					my $res = Net::Bonjour->new('http');
// 					$res->discover;
// 					$entries = [$res->entries];
// 				}
// 				if (@{$entries}) {
// 					my $dlg = Slic3r::GUI::BonjourBrowser->new($self, $entries);
// 					$self->_load_key_value('octoprint_host', $dlg->GetValue . ":".$dlg->GetPort)
// 						if $dlg->ShowModal == wxID_OK;
// 				}
// 				else {
// 					auto msg_window = new wxMessageDialog(parent, "No Bonjour device found", "Device Browser", wxOK | wxICON_INFORMATION);
// 					msg_window->ShowModal();
// 				}
			});

			return sizer;
		};

		auto octoprint_host_test = [this](wxWindow* parent) {
			auto btn = m_octoprint_host_test_btn = new wxButton(parent, wxID_ANY, _L("Test"), 
				wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
//			btn->SetFont($Slic3r::GUI::small_font);
			btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("wrench.png").c_str()), wxBITMAP_TYPE_PNG));
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent e) {
				if (m_event_button_test > 0){
					wxCommandEvent event(m_event_button_test);
					event.SetString(_L("Button TEST was clicked!"));
					g_wxMainFrame->ProcessWindowEvent(event);
				}
// 				my $ua = LWP::UserAgent->new;
// 				$ua->timeout(10);
// 
// 				my $res = $ua->get(
// 					"http://".$self->{config}->octoprint_host . "/api/version",
// 					'X-Api-Key' = > $self->{config}->octoprint_apikey,
// 					);
// 				if ($res->is_success) {
// 					show_info(parent, "Connection to OctoPrint works correctly.", "Success!");
// 				}
// 				else {
// 					show_error(parent, 
// 						"I wasn't able to connect to OctoPrint (".$res->status_line . "). "
// 						. "Check hostname and OctoPrint version (at least 1.1.0 is required).");
// 				}
 			});
			return sizer;
		};

		Line host_line = optgroup->create_single_option_line("octoprint_host");
		host_line.append_widget(octoprint_host_browse);
		host_line.append_widget(octoprint_host_test);
		optgroup->append_line(host_line);
		optgroup->append_single_option_line("octoprint_apikey");

		optgroup = page->new_optgroup(_L("Firmware"));
		optgroup->append_single_option_line("gcode_flavor");

		optgroup = page->new_optgroup(_L("Advanced"));
		optgroup->append_single_option_line("use_relative_e_distances");
		optgroup->append_single_option_line("use_firmware_retraction");
		optgroup->append_single_option_line("use_volumetric_e");
		optgroup->append_single_option_line("variable_layer_height");

	page = add_options_page(_L("Custom G-code"), "cog.png");
		optgroup = page->new_optgroup(_L("Start G-code"), 0);
		option = optgroup->get_option("start_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_L("End G-code"), 0);
		option = optgroup->get_option("end_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_L("Before layer change G-code"), 0);
		option = optgroup->get_option("before_layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_L("After layer change G-code"), 0);
		option = optgroup->get_option("layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_L("Tool change G-code"), 0);
		option = optgroup->get_option("toolchange_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_L("Between objects G-code (for sequential printing)"), 0);
		option = optgroup->get_option("between_objects_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);
	
	page = add_options_page(_L("Notes"), "note.png");
		optgroup = page->new_optgroup(_L("Notes"), 0);
		option = optgroup->get_option("printer_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	build_extruder_pages();

	if (!m_no_controller)
		update_serial_ports();
}

void TabPrinter::update_serial_ports(){
	Field *field = get_field("serial_port");
	Choice *choice = static_cast<Choice *>(field);
	choice->set_values(scan_serial_ports());
}

void TabPrinter::extruders_count_changed(size_t extruders_count){
	m_extruders_count = extruders_count;
	m_preset_bundle->printers.get_edited_preset().set_num_extruders(extruders_count);
	m_preset_bundle->update_multi_material_filament_presets();
	build_extruder_pages();
	on_value_change("extruders_count", extruders_count);
}

void TabPrinter::build_extruder_pages(){
	for (auto extruder_idx = m_extruder_pages.size(); extruder_idx < m_extruders_count; ++extruder_idx){
		//# build page
		auto page = add_options_page(_L("Extruder ") + wxString::Format(_T("%i"), extruder_idx + 1), "funnel.png", true);
		m_extruder_pages.push_back(page);
			
			auto optgroup = page->new_optgroup(_L("Size"));
			optgroup->append_single_option_line("nozzle_diameter", extruder_idx);
		
			optgroup = page->new_optgroup(_L("Layer height limits"));
			optgroup->append_single_option_line("min_layer_height", extruder_idx);
			optgroup->append_single_option_line("max_layer_height", extruder_idx);
				
		
			optgroup = page->new_optgroup(_L("Position (for multi-extruder printers)"));
			optgroup->append_single_option_line("extruder_offset", extruder_idx);
		
			optgroup = page->new_optgroup(_L("Retraction"));
			optgroup->append_single_option_line("retract_length", extruder_idx);
			optgroup->append_single_option_line("retract_lift", extruder_idx);
				Line line = { _L("Only lift Z"), "" };
				line.append_option(optgroup->get_option("retract_lift_above", extruder_idx));
				line.append_option(optgroup->get_option("retract_lift_below", extruder_idx));
				optgroup->append_line(line);
			
			optgroup->append_single_option_line("retract_speed", extruder_idx);
			optgroup->append_single_option_line("deretract_speed", extruder_idx);
			optgroup->append_single_option_line("retract_restart_extra", extruder_idx);
			optgroup->append_single_option_line("retract_before_travel", extruder_idx);
			optgroup->append_single_option_line("retract_layer_change", extruder_idx);
			optgroup->append_single_option_line("wipe", extruder_idx);
			optgroup->append_single_option_line("retract_before_wipe", extruder_idx);
	
			optgroup = page->new_optgroup(_L("Retraction when tool is disabled (advanced settings for multi-extruder setups)"));
			optgroup->append_single_option_line("retract_length_toolchange", extruder_idx);
			optgroup->append_single_option_line("retract_restart_extra_toolchange", extruder_idx);

			optgroup = page->new_optgroup(_L("Preview"));
			optgroup->append_single_option_line("extruder_colour", extruder_idx);
	}
 
	// # remove extra pages
	if (m_extruders_count <= m_extruder_pages.size()) {
		m_extruder_pages.resize(m_extruders_count);
	}

	// # rebuild page list
	PageShp page_note = m_pages.back();
	m_pages.pop_back();
	while (m_pages.back()->title().find(_L("Extruder")) != std::string::npos)
		m_pages.pop_back();
	for (auto page_extruder : m_extruder_pages)
		m_pages.push_back(page_extruder);
	m_pages.push_back(page_note);
	rebuild_page_tree();
}

// this gets executed after preset is loaded and before GUI fields are updated
void TabPrinter::on_preset_loaded()
{
	// update the extruders count field
	auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"));
	int extruders_count = nozzle_diameter->values.size();
	set_value("extruders_count", extruders_count);
	// update the GUI field according to the number of nozzle diameters supplied
	extruders_count_changed(extruders_count);
}

void TabPrinter::update(){
	Freeze();

	bool en;
	auto serial_speed = get_field("serial_speed");
	if (serial_speed != nullptr) {
		en = !m_config->opt_string("serial_port").empty();
		get_field("serial_speed")->toggle(en);
		if (m_config->opt_int("serial_speed") != 0 && en)
			m_serial_test_btn->Enable();
		else 
			m_serial_test_btn->Disable();
	}

	en = !m_config->opt_string("octoprint_host").empty();
	if ( en && m_is_user_agent)
		m_octoprint_host_test_btn->Enable();
	else 
		m_octoprint_host_test_btn->Disable(); 
	get_field("octoprint_apikey")->toggle(en);

	bool have_multiple_extruders = m_extruders_count > 1;
	get_field("toolchange_gcode")->toggle(have_multiple_extruders);
	get_field("single_extruder_multi_material")->toggle(have_multiple_extruders);

	for (size_t i = 0; i < m_extruders_count; ++i) {
		bool have_retract_length = m_config->opt_float("retract_length", i) > 0;

		// when using firmware retraction, firmware decides retraction length
		bool use_firmware_retraction = m_config->opt_bool("use_firmware_retraction");
		get_field("retract_length", i)->toggle(!use_firmware_retraction);

		// user can customize travel length if we have retraction length or we"re using
		// firmware retraction
		get_field("retract_before_travel", i)->toggle(have_retract_length || use_firmware_retraction);

		// user can customize other retraction options if retraction is enabled
		bool retraction = (have_retract_length || use_firmware_retraction);
		std::vector<std::string> vec = { "retract_lift", "retract_layer_change" };
		for (auto el : vec)
			get_field(el, i)->toggle(retraction);

		// retract lift above / below only applies if using retract lift
		vec.resize(0);
		vec = { "retract_lift_above", "retract_lift_below" };
		for (auto el : vec)
			get_field(el, i)->toggle(retraction && m_config->opt_float("retract_lift", i) > 0);

		// some options only apply when not using firmware retraction
		vec.resize(0);
		vec = { "retract_speed", "deretract_speed", "retract_before_wipe", "retract_restart_extra", "wipe" };
		for (auto el : vec)
			get_field(el, i)->toggle(retraction && !use_firmware_retraction);

		bool wipe = m_config->opt_bool("wipe", i);
		get_field("retract_before_wipe", i)->toggle(wipe);

		if (use_firmware_retraction && wipe) {
			auto dialog = new wxMessageDialog(parent(),
				_L("The Wipe option is not available when using the Firmware Retraction mode.\n"
				"\nShall I disable it in order to enable Firmware Retraction?"),
				_L("Firmware Retraction"), wxICON_WARNING | wxYES | wxNO);

			DynamicPrintConfig new_conf = *m_config;
			if (dialog->ShowModal() == wxID_YES) {
				auto wipe = static_cast<ConfigOptionBools*>(m_config->option("wipe"));
				wipe->values[i] = 0;
				new_conf.set_key_value("wipe", wipe);
			}
			else {
				new_conf.set_key_value("use_firmware_retraction", new ConfigOptionBool(false));
			}
			load_config(new_conf);
		}

		get_field("retract_length_toolchange", i)->toggle(have_multiple_extruders);

		bool toolchange_retraction = m_config->opt_float("retract_length_toolchange", i) > 0;
		get_field("retract_restart_extra_toolchange", i)->toggle
			(have_multiple_extruders && toolchange_retraction);
	}

	Thaw();
}

// Initialize the UI from the current preset
void Tab::load_current_preset()
{
	auto preset = m_presets->get_edited_preset();
//	try{
//		local $SIG{ __WARN__ } = Slic3r::GUI::warning_catcher($self);
		preset.is_default ? m_btn_delete_preset->Disable() : m_btn_delete_preset->Enable(true);
		update();
		// For the printer profile, generate the extruder pages.
		on_preset_loaded();
		// Reload preset pages with the new configuration values.
		reload_config();
//	};
	// use CallAfter because some field triggers schedule on_change calls using CallAfter,
	// and we don't want them to be called after this update_dirty() as they would mark the 
	// preset dirty again
	// (not sure this is true anymore now that update_dirty is idempotent)
	wxTheApp->CallAfter([this]{
		// checking out if this Tab exists till this moment
		if (!checked_tab(this))
			return;
		update_tab_ui();
		on_presets_changed();
	});
}

//Regerenerate content of the page tree.
void Tab::rebuild_page_tree()
{
	Freeze();
	// get label of the currently selected item
	auto selected = m_treectrl->GetItemText(m_treectrl->GetSelection());
	auto rootItem = m_treectrl->GetRootItem();
	m_treectrl->DeleteChildren(rootItem);
	auto have_selection = 0;
	for (auto p : m_pages)
	{
		auto itemId = m_treectrl->AppendItem(rootItem, p->title(), p->iconID());
		if (p->title() == selected) {
			m_disable_tree_sel_changed_event = 1;
			m_treectrl->SelectItem(itemId);
			m_disable_tree_sel_changed_event = 0;
			have_selection = 1;
		}
	}
	
	if (!have_selection) {
		// this is triggered on first load, so we don't disable the sel change event
		m_treectrl->SelectItem(m_treectrl->GetFirstVisibleItem());//! (treectrl->GetFirstChild(rootItem));
	}
	Thaw();
}

// Called by the UI combo box when the user switches profiles.
// Select a preset by a name.If !defined(name), then the default preset is selected.
// If the current profile is modified, user is asked to save the changes.
void Tab::select_preset(std::string preset_name /*= ""*/)
{
	std::string name = preset_name;
	auto force = false;
	auto presets = m_presets;
	// If no name is provided, select the "-- default --" preset.
	if (name.empty())
		name= presets->default_preset().name;
	auto current_dirty = presets->current_is_dirty();
	auto canceled = false;
	auto printer_tab = presets->name().compare("printer")==0;
	m_reload_dependent_tabs = {};
	if (!force && current_dirty && !may_discard_current_dirty_preset()) {
		canceled = true;
	} else if(printer_tab) {
		// Before switching the printer to a new one, verify, whether the currently active print and filament
		// are compatible with the new printer.
		// If they are not compatible and the current print or filament are dirty, let user decide
		// whether to discard the changes or keep the current printer selection.
		auto new_printer_preset = presets->find_preset(name, true);
		auto print_presets = &m_preset_bundle->prints;
		bool print_preset_dirty = print_presets->current_is_dirty();
		bool print_preset_compatible = print_presets->get_edited_preset().is_compatible_with_printer(*new_printer_preset);
		canceled = !force && print_preset_dirty && !print_preset_compatible &&
			!may_discard_current_dirty_preset(print_presets, name);
		auto filament_presets = &m_preset_bundle->filaments;
		bool filament_preset_dirty = filament_presets->current_is_dirty();
		bool filament_preset_compatible = filament_presets->get_edited_preset().is_compatible_with_printer(*new_printer_preset);
		if (!canceled && !force) {
			canceled = filament_preset_dirty && !filament_preset_compatible &&
				!may_discard_current_dirty_preset(filament_presets, name);
		}
		if (!canceled) {
			if (!print_preset_compatible) {
				// The preset will be switched to a different, compatible preset, or the '-- default --'.
				m_reload_dependent_tabs.push_back("print");
				if (print_preset_dirty) print_presets->discard_current_changes();
			}
			if (!filament_preset_compatible) {
				// The preset will be switched to a different, compatible preset, or the '-- default --'.
				m_reload_dependent_tabs.push_back("filament");
				if (filament_preset_dirty) filament_presets->discard_current_changes();
			}
		}
	}
	if (canceled) {
		update_tab_ui();
		// Trigger the on_presets_changed event so that we also restore the previous value in the plater selector,
		// if this action was initiated from the platter.
		on_presets_changed();
	}
	else {
		if (current_dirty) presets->discard_current_changes() ;
		presets->select_preset_by_name(name, force);
		// Mark the print & filament enabled if they are compatible with the currently selected preset.
		// The following method should not discard changes of current print or filament presets on change of a printer profile,
		// if they are compatible with the current printer.
		if (current_dirty || printer_tab)
			m_preset_bundle->update_compatible_with_printer(true);
		// Initialize the UI from the current preset.
		load_current_preset();
	}

}

// If the current preset is dirty, the user is asked whether the changes may be discarded.
// if the current preset was not dirty, or the user agreed to discard the changes, 1 is returned.
bool Tab::may_discard_current_dirty_preset(PresetCollection* presets /*= nullptr*/, std::string new_printer_name /*= ""*/)
{
	if (presets == nullptr) presets = m_presets;
	// Display a dialog showing the dirty options in a human readable form.
	auto old_preset = presets->get_edited_preset();
	auto type_name = presets->name();
	auto tab = "          ";
	auto name = old_preset.is_default ?
		_L("Default ") + type_name + _L(" preset") :
		(type_name + _L(" preset\n") + tab + old_preset.name);
	// Collect descriptions of the dirty options.
	std::vector<std::string> option_names;
	for(auto opt_key: presets->current_dirty_options()) {
		auto opt = m_config->def()->options.at(opt_key);
		std::string name = "";
		if (!opt.category.empty())
			name += opt.category + " > ";
		name += !opt.full_label.empty() ?
				opt.full_label : 
				opt.label;
		option_names.push_back(name);
	}
	// Show a confirmation dialog with the list of dirty options.
	std::string changes = "";
	for (auto changed_name : option_names)
		changes += tab + changed_name + "\n";
	auto message = (!new_printer_name.empty()) ?
		name + _L("\n\nis not compatible with printer\n") +tab + new_printer_name+ _L("\n\nand it has the following unsaved changes:") :
		name + _L("\n\nhas the following unsaved changes:");
	auto confirm = new wxMessageDialog(parent(),
		message + "\n" +changes +_L("\n\nDiscard changes and continue anyway?"),
		_L("Unsaved Changes"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
	return confirm->ShowModal() == wxID_YES;
}

void Tab::OnTreeSelChange(wxTreeEvent& event)
{
	if (m_disable_tree_sel_changed_event) return;
	Page* page = nullptr;
	auto selection = m_treectrl->GetItemText(m_treectrl->GetSelection());
	for (auto p : m_pages)
		if (p->title() == selection)
		{
			page = p.get();
			break;
		}
	if (page == nullptr) return;

	for (auto& el : m_pages)
		el.get()->Hide();
	page->Show();
	m_hsizer->Layout();
	Refresh();
}

void Tab::OnKeyDown(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_TAB)
		m_treectrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
	else
		event.Skip();
}

// Save the current preset into file.
// This removes the "dirty" flag of the preset, possibly creates a new preset under a new name,
// and activates the new preset.
// Wizard calls save_preset with a name "My Settings", otherwise no name is provided and this method
// opens a Slic3r::GUI::SavePresetWindow dialog.
void Tab::save_preset(std::string name /*= ""*/)
{
	// since buttons(and choices too) don't get focus on Mac, we set focus manually
	// to the treectrl so that the EVT_* events are fired for the input field having
	// focus currently.is there anything better than this ?
//!	m_treectrl->OnSetFocus();

	if (name.empty()) {
		auto preset = m_presets->get_selected_preset();
		auto default_name = preset.is_default ? "Untitled" : preset.name;
 		bool have_extention = boost::iends_with(default_name, ".ini");
		if (have_extention)
		{
			size_t len = default_name.length()-4;
			default_name.resize(len);
		}
		//[map $_->name, grep !$_->default && !$_->external, @{$self->{presets}}],
		std::vector<std::string> values;
		for (size_t i = 0; i < m_presets->size(); ++i) {
			const Preset &preset = m_presets->preset(i);
			if (preset.is_default || preset.is_external)
				continue;
			values.push_back(preset.name);
		}

		auto dlg = new SavePresetWindow(parent());
		dlg->build(title(), default_name, values);	
		if (dlg->ShowModal() != wxID_OK)
			return;
		name = dlg->get_name();
		if (name == ""){
			show_error(this, _L("The supplied name is empty. It can't be saved."));
			return;
		}
	}
	try
	{
		// Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
		m_presets->save_current_preset(name);
	}
	catch (const std::exception &e)
	{
		show_error(this, _L("Something is wrong. It can't be saved."));
		return;
	}

	// Mark the print & filament enabled if they are compatible with the currently selected preset.
	m_preset_bundle->update_compatible_with_printer(false);
	// Add the new item into the UI component, remove dirty flags and activate the saved item.
	update_tab_ui();
	// Update the selection boxes at the platter.
	on_presets_changed();
}

// Called for a currently selected preset.
void Tab::delete_preset()
{
	auto current_preset = m_presets->get_selected_preset();
	// Don't let the user delete the ' - default - ' configuration.
	wxString action = current_preset.is_external ? _L("remove") : _L("delete");
	wxString msg = _L("Are you sure you want to ") + action + _L(" the selected preset?");
	action = current_preset.is_external ? _L("Remove") : _L("Delete");
	wxString title = action + _L(" Preset");
	if (current_preset.is_default ||
		wxID_YES != wxMessageDialog(parent(), msg, title, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal())
		return;
	// Delete the file and select some other reasonable preset.
	// The 'external' presets will only be removed from the preset list, their files will not be deleted.
	try{ m_presets->delete_current_preset(); }
	catch (const std::exception &e)
	{
		return;
	}
	// Load the newly selected preset into the UI, update selection combo boxes with their dirty flags.
	load_current_preset();
}

void Tab::toggle_show_hide_incompatible()
{
	m_show_incompatible_presets = !m_show_incompatible_presets;
	update_show_hide_incompatible_button();
	update_tab_ui();
}

void Tab::update_show_hide_incompatible_button()
{
	m_btn_hide_incompatible_presets->SetBitmap(m_show_incompatible_presets ?
		*m_bmp_show_incompatible_presets : *m_bmp_hide_incompatible_presets);
	m_btn_hide_incompatible_presets->SetToolTip(m_show_incompatible_presets ?
		"Both compatible an incompatible presets are shown. Click to hide presets not compatible with the current printer." :
		"Only compatible presets are shown. Click to show both the presets compatible and not compatible with the current printer.");
}

void Tab::update_ui_from_settings()
{
	// Show the 'show / hide presets' button only for the print and filament tabs, and only if enabled
	// in application preferences.
	bool show = m_show_btn_incompatible_presets && m_presets->name().compare("printer") != 0;
	show ? m_btn_hide_incompatible_presets->Show() :  m_btn_hide_incompatible_presets->Hide();
	// If the 'show / hide presets' button is hidden, hide the incompatible presets.
	if (show) {
		update_show_hide_incompatible_button();
	}
	else {
		if (m_show_incompatible_presets) {
			m_show_incompatible_presets = false;
			update_tab_ui();
		}
	}
}

// Return a callback to create a Tab widget to mark the preferences as compatible / incompatible to the current printer.
wxSizer* Tab::compatible_printers_widget(wxWindow* parent, wxCheckBox** checkbox, wxButton** btn)
{
	*checkbox = new wxCheckBox(parent, wxID_ANY, _L("All"));
	*btn = new wxButton(parent, wxID_ANY, _L("Set")+"\u2026", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);

	(*btn)->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("printer_empty.png").c_str()), wxBITMAP_TYPE_PNG));

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add((*checkbox), 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add((*btn), 0, wxALIGN_CENTER_VERTICAL);

	(*checkbox)->Bind(wxEVT_CHECKBOX, ([=](wxCommandEvent e)
	{
		(*btn)->Enable(!(*checkbox)->GetValue());
		// All printers have been made compatible with this preset.
		if ((*checkbox)->GetValue())
			load_key_value("compatible_printers", std::vector<std::string> {});
		get_field("compatible_printers_condition")->toggle((*checkbox)->GetValue());
	}) );

	(*btn)->Bind(wxEVT_BUTTON, ([this, parent, checkbox, btn](wxCommandEvent e)
	{
		// # Collect names of non-default non-external printer profiles.
		PresetCollection *printers = &m_preset_bundle->printers;
		wxArrayString presets;
		for (size_t idx = 0; idx < printers->size(); ++idx)
		{
			Preset& preset = printers->preset(idx);
			if (!preset.is_default && !preset.is_external)
				presets.Add(preset.name);
		}

		auto dlg = new wxMultiChoiceDialog(parent,
		_L("Select the printers this profile is compatible with."),
		_L("Compatible printers"),  presets);
		// # Collect and set indices of printers marked as compatible.
		wxArrayInt selections;
		auto *compatible_printers = dynamic_cast<const ConfigOptionStrings*>(m_config->option("compatible_printers"));
		if (compatible_printers != nullptr || !compatible_printers->values.empty())
			for (auto preset_name : compatible_printers->values)
				for (size_t idx = 0; idx < presets.GetCount(); ++idx)
					if (presets[idx].compare(preset_name) == 0)
					{
						selections.Add(idx);
						break;
					}
		dlg->SetSelections(selections);
		std::vector<std::string> value;
		// Show the dialog.
		if (dlg->ShowModal() == wxID_OK) {
			selections.Clear();
			selections = dlg->GetSelections();
			for (auto idx : selections)
				value.push_back(presets[idx].ToStdString());
			if (value.empty()) {
				(*checkbox)->SetValue(1);
				(*btn)->Disable();
			}
			// All printers have been made compatible with this preset.
			load_key_value("compatible_printers", value);
		}
	}));
	return sizer; 
}

void Page::reload_config()
{
	for (auto group : m_optgroups)
		group->reload_config();
}

Field* Page::get_field(t_config_option_key opt_key, int opt_index/* = -1*/) const
{
	Field* field = nullptr;
	for (auto opt : m_optgroups){
		field = opt->get_fieldc(opt_key, opt_index);
		if (field != nullptr)
			return field;
	}
	return field;
}

bool Page::set_value(t_config_option_key opt_key, boost::any value){
	bool changed = false;
	for(auto optgroup: m_optgroups) {
		if (optgroup->set_value(opt_key, value))
			changed = 1 ;
	}
	return changed;
}

// package Slic3r::GUI::Tab::Page;
ConfigOptionsGroupShp Page::new_optgroup(wxString title, int noncommon_label_width /*= -1*/)
{
	//! config_ have to be "right"
	ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(this, title, m_config);
	if (noncommon_label_width >= 0)
		optgroup->label_width = noncommon_label_width;

	optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value){
		//! This function will be called from OptionGroup.
		//! Using of CallAfter is redundant.
		//! And in some cases it causes update() function to be recalled again
//!        wxTheApp->CallAfter([this, opt_key, value]() {
			static_cast<Tab*>(GetParent())->update_dirty();
			static_cast<Tab*>(GetParent())->on_value_change(opt_key, value);
//!        });
	};

	vsizer()->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 10);
	m_optgroups.push_back(optgroup);

	return optgroup;
}

void SavePresetWindow::build(wxString title, std::string default_name, std::vector<std::string> &values)
{
	auto text = new wxStaticText(this, wxID_ANY, _L("Save ") + title + _L(" as:"), 
									wxDefaultPosition, wxDefaultSize);
	m_combo = new wxComboBox(this, wxID_ANY, wxString::FromUTF8(default_name.c_str()), 
							wxDefaultPosition, wxDefaultSize, 0, 0, wxTE_PROCESS_ENTER);
	for (auto value : values)
		m_combo->Append(wxString::FromUTF8(value.c_str()));
	auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(text, 0, wxEXPAND | wxALL, 10);
	sizer->Add(m_combo, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
	sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 10);

	wxButton* btn = static_cast<wxButton*>(FindWindowById(wxID_OK, this));
	btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { accept(); });
	m_combo->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { accept(); });

	SetSizer(sizer);
	sizer->SetSizeHints(this);
}

void SavePresetWindow::accept()
{
	m_chosen_name = normalize_utf8_nfc(m_combo->GetValue().ToUTF8());
	if (!m_chosen_name.empty()) {
		const char* unusable_symbols = "<>:/\\|?*\"";
		bool is_unusable_symbol = false;
		for (size_t i = 0; i < std::strlen(unusable_symbols); i++){
			if (m_chosen_name.find_first_of(unusable_symbols[i]) != std::string::npos){
				is_unusable_symbol = true;
				break;
			}
		}
		if (is_unusable_symbol) {
			show_error(this, _L("The supplied name is not valid; the following characters are not allowed:")+" <>:/\\|?*\"");
		}
		else if (m_chosen_name.compare("- default -") == 0) {
			show_error(this, _L("The supplied name is not available."));
		}
		else {
			EndModal(wxID_OK);
		}
	}
}

} // GUI
} // Slic3r
