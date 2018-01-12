#include "../../libslic3r/GCodeSender.hpp"
#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/menu.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>

#include "Tab.h"
#include "PresetBundle.hpp"
#include "PresetHints.hpp"
#include "../../libslic3r/Utils.hpp"

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
	//! Add Preset from PrintPreset
	// choice menu for Experiments
	wxString choices[] =
	{
		_T("First"),
		_T("Second"),
		_T("Third")
	};

	m_presets_choice = new wxBitmapComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(270, -1)/*, nCntEl, choices, wxCB_READONLY*/);
	const wxBitmap* bmp = new wxBitmap(wxString::FromUTF8(Slic3r::var("flag-green-icon.png").c_str()), wxBITMAP_TYPE_PNG);
	for (auto el:choices)
		m_presets_choice->Append(wxString::FromUTF8(el).c_str(), *bmp);
	m_presets_choice->SetSelection(m_presets_choice->GetCount() - 1);

	//buttons
	wxBitmap bmpMenu;
	bmpMenu = wxBitmap(wxString::FromUTF8(Slic3r::var("disk.png").c_str()), wxBITMAP_TYPE_PNG);
	m_btn_save_preset = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	bmpMenu = wxBitmap(wxString::FromUTF8(Slic3r::var("delete.png").c_str()), wxBITMAP_TYPE_PNG);
	m_btn_delete_preset = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	//	$self->{show_incompatible_presets} = 0;			// !!!

	m_bmp_show_incompatible_presets = new wxBitmap(wxString::FromUTF8(Slic3r::var("flag-red-icon.png").c_str()), wxBITMAP_TYPE_PNG);
	m_bmp_hide_incompatible_presets = new wxBitmap(wxString::FromUTF8(Slic3r::var("flag-green-icon.png").c_str()), wxBITMAP_TYPE_PNG);
	m_btn_hide_incompatible_presets = new wxBitmapButton(panel, wxID_ANY, *m_bmp_hide_incompatible_presets, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	m_btn_save_preset->SetToolTip(wxT("Save current ") + wxString(m_title));// (stTitle);
	m_btn_delete_preset->SetToolTip(_T("Delete this preset"));
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
	m_treectrl = new wxTreeCtrl(panel, wxID_ANY/*ID_TAB_TREE*/, wxDefaultPosition, wxSize(185, -1), wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN | wxWANTS_CHARS);
	m_left_sizer->Add(m_treectrl, 1, wxEXPAND);
	m_icons = new wxImageList(16, 16, true, 1/*, 1*/);
	// Index of the last icon inserted into $self->{icons}.
	m_icon_count = -1;
	m_treectrl->AssignImageList(m_icons);
	m_treectrl->AddRoot("root");
	m_treectrl->SetIndent(0);
	m_disable_tree_sel_changed_event = 0;

	m_treectrl->Bind(wxEVT_TREE_SEL_CHANGED, &Tab::OnTreeSelChange, this);
	m_treectrl->Bind(wxEVT_KEY_DOWN, &Tab::OnKeyDown, this);
	m_treectrl->Bind(wxEVT_COMBOBOX, &Tab::OnComboBox, this); 

	m_btn_save_preset->Bind(wxEVT_BUTTON, &Tab::save_preset, this);
	m_btn_delete_preset->Bind(wxEVT_BUTTON, &Tab::delete_preset, this);
	m_btn_hide_incompatible_presets->Bind(wxEVT_BUTTON, &Tab::toggle_show_hide_incompatible, this);

	// Initialize the DynamicPrintConfig by default keys/values.
	// Possible %params keys: no_controller
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
			const auto img_icon = new wxIcon(wxString::FromUTF8(Slic3r::var(/*"" + */icon).c_str()), wxBITMAP_TYPE_PNG);
			m_icons->Add(*img_icon);
			icon_idx = ++m_icon_count; //  $icon_idx = $self->{icon_count} + 1; $self->{icon_count} = $icon_idx;
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
//	_on_presets_changed;
}

// Load a provied DynamicConfig into the tab, modifying the active preset.
// This could be used for example by setting a Wipe Tower position by interactive manipulation in the 3D view.
void Tab::load_config(DynamicPrintConfig config)
{
	bool modified = 0;
	for(auto opt_key : m_config->diff(config)) {
		switch ( config.def()->get(opt_key)->type ){
		case coFloatOrPercent:
			change_opt_value(*m_config, opt_key, config.option<ConfigOptionFloatOrPercent>(opt_key)->value);
			break;
		case coPercent:
			change_opt_value(*m_config, opt_key, config.option<ConfigOptionPercent>(opt_key)->value);
			break;
		case coFloat:
			change_opt_value(*m_config, opt_key, config.opt_float(opt_key));
			break;
//		case coPercents:
//		case coFloats:
		case coString:
			change_opt_value(*m_config, opt_key, config.opt_string(opt_key));
			break;
		case coStrings:
			break;
		case coBool:
			change_opt_value(*m_config, opt_key, config.opt_bool(opt_key));
			break;
		case coBools:
//			opt = new ConfigOptionBools(0, config.opt_bool(opt_key)); //! 0?
			break;
		case coInt:
			change_opt_value(*m_config, opt_key, config.opt_int(opt_key));
			break;
		case coInts:
			break;
		case coEnum:{
			if (opt_key.compare("external_fill_pattern") == 0 ||
				opt_key.compare("fill_pattern") == 0)
				change_opt_value(*m_config, opt_key, config.option<ConfigOptionEnum<InfillPattern>>(opt_key)->value);
			else if (opt_key.compare("gcode_flavor") == 0)
				change_opt_value(*m_config, opt_key, config.option<ConfigOptionEnum<GCodeFlavor>>(opt_key)->value);
			else if (opt_key.compare("support_material_pattern") == 0)
				change_opt_value(*m_config, opt_key, config.option<ConfigOptionEnum<SupportMaterialPattern>>(opt_key)->value);
			else if (opt_key.compare("seam_position") == 0)
				change_opt_value(*m_config, opt_key, config.option<ConfigOptionEnum<SeamPosition>>(opt_key)->value);
		}
			break;
		case coPoints:
			break;
		case coNone:
			break;
		default:
			break;
		}
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
		field = page->get_field(opt_key);
		if (field != nullptr)
			return field;
	}
	return field;
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
	m_config_def = m_config->def();

	auto page = add_options_page("Layers and perimeters", "layers.png");
		auto optgroup = page->new_optgroup("Layer height");
		optgroup->append_single_option_line("layer_height");
		optgroup->append_single_option_line("first_layer_height");

		optgroup = page->new_optgroup("Vertical shells");
		optgroup->append_single_option_line("perimeters");
		optgroup->append_single_option_line("spiral_vase");

		optgroup = page->new_optgroup("Horizontal shells");
		Line line{ "Solid layers", "" };
		line.append_option(optgroup->get_option("top_solid_layers"));
		line.append_option(optgroup->get_option("bottom_solid_layers"));
		optgroup->append_line(line);

		optgroup = page->new_optgroup("Quality (slower slicing)");
		optgroup->append_single_option_line("extra_perimeters");
		optgroup->append_single_option_line("ensure_vertical_shell_thickness");
		optgroup->append_single_option_line("avoid_crossing_perimeters");
		optgroup->append_single_option_line("thin_walls");
		optgroup->append_single_option_line("overhangs");

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line("seam_position");
		optgroup->append_single_option_line("external_perimeters_first");

	page = add_options_page("Infill", "infill.png");
		optgroup = page->new_optgroup("Infill");
		optgroup->append_single_option_line("fill_density");
		optgroup->append_single_option_line("fill_pattern");
		optgroup->append_single_option_line("external_fill_pattern");

		optgroup = page->new_optgroup("Reducing printing time");
		optgroup->append_single_option_line("infill_every_layers");
		optgroup->append_single_option_line("infill_only_where_needed");

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line("solid_infill_every_layers");
		optgroup->append_single_option_line("fill_angle");
		optgroup->append_single_option_line("solid_infill_below_area");
		optgroup->append_single_option_line("bridge_angle");
		optgroup->append_single_option_line("only_retract_when_crossing_perimeters");
		optgroup->append_single_option_line("infill_first");

	page = add_options_page("Skirt and brim", "box.png");
		optgroup = page->new_optgroup("Skirt");
		optgroup->append_single_option_line("skirts");
		optgroup->append_single_option_line("skirt_distance");
		optgroup->append_single_option_line("skirt_height");
		optgroup->append_single_option_line("min_skirt_length");

		optgroup = page->new_optgroup("Brim");
		optgroup->append_single_option_line("brim_width");

	page = add_options_page("Support material", "building.png");
		optgroup = page->new_optgroup("Support material");
		optgroup->append_single_option_line("support_material");
		optgroup->append_single_option_line("support_material_threshold");
		optgroup->append_single_option_line("support_material_enforce_layers");

		optgroup = page->new_optgroup("Raft");
		optgroup->append_single_option_line("raft_layers");
//		# optgroup->append_single_option_line(get_option_("raft_contact_distance");

		optgroup = page->new_optgroup("Options for support material and raft");
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

	page = add_options_page("Speed", "time.png");
		optgroup = page->new_optgroup("Speed for print moves");
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

		optgroup = page->new_optgroup("Speed for non-print moves");
		optgroup->append_single_option_line("travel_speed");

		optgroup = page->new_optgroup("Modifiers");
		optgroup->append_single_option_line("first_layer_speed");

		optgroup = page->new_optgroup("Acceleration control (advanced)");
		optgroup->append_single_option_line("perimeter_acceleration");
		optgroup->append_single_option_line("infill_acceleration");
		optgroup->append_single_option_line("bridge_acceleration");
		optgroup->append_single_option_line("first_layer_acceleration");
		optgroup->append_single_option_line("default_acceleration");

		optgroup = page->new_optgroup("Autospeed (advanced)");
		optgroup->append_single_option_line("max_print_speed");
		optgroup->append_single_option_line("max_volumetric_speed");
		optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_positive");
		optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_negative");

	page = add_options_page("Multiple Extruders", "funnel.png");
		optgroup = page->new_optgroup("Extruders");
		optgroup->append_single_option_line("perimeter_extruder");
		optgroup->append_single_option_line("infill_extruder");
		optgroup->append_single_option_line("solid_infill_extruder");
		optgroup->append_single_option_line("support_material_extruder");
		optgroup->append_single_option_line("support_material_interface_extruder");

		optgroup = page->new_optgroup("Ooze prevention");
		optgroup->append_single_option_line("ooze_prevention");
		optgroup->append_single_option_line("standby_temperature_delta");

		optgroup = page->new_optgroup("Wipe tower");
		optgroup->append_single_option_line("wipe_tower");
		optgroup->append_single_option_line("wipe_tower_x");
		optgroup->append_single_option_line("wipe_tower_y");
		optgroup->append_single_option_line("wipe_tower_width");
		optgroup->append_single_option_line("wipe_tower_per_color_wipe");

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line("interface_shells");

	page = add_options_page("Advanced", "wrench.png");
		optgroup = page->new_optgroup("Extrusion width", 180);
		optgroup->append_single_option_line("extrusion_width");
		optgroup->append_single_option_line("first_layer_extrusion_width");
		optgroup->append_single_option_line("perimeter_extrusion_width");
		optgroup->append_single_option_line("external_perimeter_extrusion_width");
		optgroup->append_single_option_line("infill_extrusion_width");
		optgroup->append_single_option_line("solid_infill_extrusion_width");
		optgroup->append_single_option_line("top_infill_extrusion_width");
		optgroup->append_single_option_line("support_material_extrusion_width");

		optgroup = page->new_optgroup("Overlap");
		optgroup->append_single_option_line("infill_overlap");

		optgroup = page->new_optgroup("Flow");
		optgroup->append_single_option_line("bridge_flow_ratio");

		optgroup = page->new_optgroup("Other");
		optgroup->append_single_option_line("clip_multipart_objects");
		optgroup->append_single_option_line("elefant_foot_compensation");
		optgroup->append_single_option_line("xy_size_compensation");
//		#            optgroup->append_single_option_line("threads");
		optgroup->append_single_option_line("resolution");

	page = add_options_page("Output options", "page_white_go.png");
		optgroup = page->new_optgroup("Sequential printing");
		optgroup->append_single_option_line("complete_objects");
		line = Line{ "Extruder clearance (mm)", "" };
		Option option = optgroup->get_option("extruder_clearance_radius");
		option.opt.width = 60;
		line.append_option(option);
		option = optgroup->get_option("extruder_clearance_height");
		option.opt.width = 60;
		line.append_option(option);
		optgroup->append_line(line);

		optgroup = page->new_optgroup("Output file");
		optgroup->append_single_option_line("gcode_comments");
		option = optgroup->get_option("output_filename_format");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Post-processing scripts", 0);	
		option = optgroup->get_option("post_process");
		option.opt.full_width = true;
		option.opt.height = 50;
		optgroup->append_single_option_line(option);

	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes", 0);						
		option = optgroup->get_option("notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page("Dependencies", "wrench.png");
		optgroup = page->new_optgroup("Profile dependencies");
		line = Line{ "Compatible printers", "" };
		line.widget = [this](wxWindow* parent){
			m_compatible_printers_checkbox = new wxCheckBox(parent, wxID_ANY, "All");
			m_compatible_printers_btn = new wxButton(parent, wxID_ANY, "Set…", wxDefaultPosition,
				wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
			return compatible_printers_widget(parent, m_compatible_printers_checkbox, m_compatible_printers_btn);
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
		!(m_config->opt_int("perimeters") == 1 && m_config->opt_int("top_solid_layers") == 0 && /*m_config->opt_float("fill_density") == 0*/
			m_config->option<ConfigOptionPercent>("fill_density")->value == 0)) {
		std::string msg_text = "The Spiral Vase mode requires:\n"
			"- one perimeter\n"
 			"- no top solid layers\n"
 			"- 0% fill density\n"
 			"- no support material\n"
 			"- no ensure_vertical_shell_thickness\n"
  			"\nShall I adjust those settings in order to enable Spiral Vase?";
		auto dialog = new wxMessageDialog(parent(), msg_text, wxT("Spiral Vase"), wxICON_WARNING | wxYES | wxNO);
		DynamicPrintConfig new_conf = *m_config;//new DynamicPrintConfig;
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

	if (m_config->opt_bool("wipe_tower") &&
		(m_config->option<ConfigOptionFloatOrPercent>("first_layer_height")->value != 0.2 /*$config->first_layer_height != 0.2*/ || 
			m_config->opt_float("layer_height") < 0.15 || m_config->opt_float("layer_height") > 0.35)) {
		std::string msg_text = "The Wipe Tower currently supports only:\n"
			"- first layer height 0.2mm\n"
			"- layer height from 0.15mm to 0.35mm\n"
			"\nShall I adjust those settings in order to enable the Wipe Tower?";
		auto dialog = new wxMessageDialog(parent(), msg_text, wxT("Wipe Tower"), wxICON_WARNING | wxYES | wxNO);
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
		std::string msg_text = "The Wipe Tower currently supports the non-soluble supports only\n"
			"if they are printed with the current extruder without triggering a tool change.\n"
			"(both support_material_extruder and support_material_interface_extruder need to be set to 0).\n"
			"\nShall I adjust those settings in order to enable the Wipe Tower?";
		auto dialog = new wxMessageDialog(parent(), msg_text, wxT("Wipe Tower"), wxICON_WARNING | wxYES | wxNO);
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
		std::string msg_text = "For the Wipe Tower to work with the soluble supports, the support layers\n"
			"need to be synchronized with the object layers.\n"
			"\nShall I synchronize support layers in order to enable the Wipe Tower?";
		auto dialog = new wxMessageDialog(parent(), msg_text, wxT("Wipe Tower"), wxICON_WARNING | wxYES | wxNO);
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
				std::string msg_text = "Supports work better, if the following feature is enabled:\n"
					"- Detect bridging perimeters\n"
					"\nShall I adjust those settings for supports?";
				auto dialog = new wxMessageDialog(parent(), msg_text, wxT("Support Generator"), wxICON_WARNING | wxYES | wxNO | wxCANCEL);
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
				std::string msg_text = "The " + str_fill_pattern + " infill pattern is not supposed to work at 100% density.\n"
					"\nShall I switch to rectilinear fill pattern?";
				auto dialog = new wxMessageDialog(parent(), msg_text, wxT("Infill"), wxICON_WARNING | wxYES | wxNO);
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

	Thaw();
}

void TabFilament::build()
{
	m_presets = &m_preset_bundle->filaments;
	m_config = &m_preset_bundle->filaments.get_edited_preset().config;
	m_config_def = m_config->def();

	auto page = add_options_page("Filament", "spool.png");
		auto optgroup = page->new_optgroup("Filament");
		optgroup->append_single_option_line("filament_colour");
		optgroup->append_single_option_line("filament_diameter");
		optgroup->append_single_option_line("extrusion_multiplier");
		optgroup->append_single_option_line("filament_density");
		optgroup->append_single_option_line("filament_cost");

		optgroup = page->new_optgroup("Temperature (°C)");
		Line line = { "Extruder", "" };
		line.append_option(optgroup->get_option("first_layer_temperature"));
		line.append_option(optgroup->get_option("temperature"));
		optgroup->append_line(line);

		line = { "Bed", "" };
		line.append_option(optgroup->get_option("first_layer_bed_temperature"));
		line.append_option(optgroup->get_option("bed_temperature"));
		optgroup->append_line(line);

	page = add_options_page("Cooling", "hourglass.png");
		optgroup = page->new_optgroup("Enable");
		optgroup->append_single_option_line("fan_always_on");
		optgroup->append_single_option_line("cooling");

		line = { "", "" }; 
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			m_cooling_description_line = new wxStaticText(parent, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
			return description_line_widget(parent, m_cooling_description_line);
		};
		optgroup->append_line(line);

		optgroup = page->new_optgroup("Fan settings");
		line = {"Fan speed",""};
		line.append_option(optgroup->get_option("min_fan_speed"));
		line.append_option(optgroup->get_option("max_fan_speed"));
		optgroup->append_line(line);

		optgroup->append_single_option_line("bridge_fan_speed");
		optgroup->append_single_option_line("disable_fan_first_layers");

		optgroup = page->new_optgroup("Cooling thresholds", 250);
		optgroup->append_single_option_line("fan_below_layer_time");
		optgroup->append_single_option_line("slowdown_below_layer_time");
		optgroup->append_single_option_line("min_print_speed");

	page = add_options_page("Advanced", "wrench.png");
		optgroup = page->new_optgroup("Filament properties");
		optgroup->append_single_option_line("filament_type");
		optgroup->append_single_option_line("filament_soluble");

		optgroup = page->new_optgroup("Print speed override");
		optgroup->append_single_option_line("filament_max_volumetric_speed");

		line = {"",""};
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			m_volumetric_speed_description_line = new wxStaticText(parent, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
			return description_line_widget(parent, m_volumetric_speed_description_line);
		};
		optgroup->append_line(line);

	page = add_options_page("Custom G-code", "cog.png");
		optgroup = page->new_optgroup("Start G-code", 0);
		Option option = optgroup->get_option("start_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("End G-code", 0);
		option = optgroup->get_option("end_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes", 0);
		optgroup->label_width = 0;
		option = optgroup->get_option("filament_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page("Dependencies", "wrench.png");
		optgroup = page->new_optgroup("Profile dependencies");
		line = {"Compatible printers", ""};
		line.widget = [this](wxWindow* parent){
			m_compatible_printers_checkbox = new wxCheckBox(parent, wxID_ANY, "All");
			m_compatible_printers_btn = new wxButton(parent, wxID_ANY, "Set…", wxDefaultPosition, 
													wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
			return compatible_printers_widget(parent, m_compatible_printers_checkbox, m_compatible_printers_btn);
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
	m_cooling_description_line->SetLabel(text);
	text = wxString::FromUTF8(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle).c_str());
	m_volumetric_speed_description_line->SetLabel(text);

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
	m_volumetric_speed_description_line->SetLabel(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle));
}

wxSizer* TabFilament::description_line_widget(wxWindow* parent, wxStaticText* StaticText)
{
	auto font = (new wxSystemSettings)->GetFont(wxSYS_DEFAULT_GUI_FONT);
	StaticText->SetFont(font);
	StaticText->GetParent()->Layout();

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(StaticText);
	return sizer;
}

void TabPrinter::build()
{
	m_presets = &m_preset_bundle->printers;
	m_config = &m_preset_bundle->printers.get_edited_preset().config;
	m_config_def = m_config->def();		// It will be used in get_option_(const std::string title)
	auto default_config = m_preset_bundle->full_config();

	auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"));
	m_extruders_count = nozzle_diameter->values.size();

	auto page = add_options_page("General", "printer_empty.png");
		auto optgroup = page->new_optgroup("Size and coordinates");

		Line line = { "Bed shape", "" };
		line.widget = [](wxWindow* parent){
			auto btn = new wxButton(parent, wxID_ANY, "Set…", wxDefaultPosition, wxDefaultSize,
				wxBU_LEFT | wxBU_EXACTFIT);
//			btn->SetFont(Slic3r::GUI::small_font);
			btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("printer_empty.png").c_str()), wxBITMAP_TYPE_PNG));

			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, ([](wxCommandEvent e)
			{
				// 			auto dlg = new BedShapeDialog->new($self, $self->{config}->bed_shape);
				// 			if (dlg->ShowModal == wxID_OK)
				;// load_key_value_("bed_shape", dlg->GetValue);
			}));

			return sizer;
		};
		optgroup->append_line(line);
		optgroup->append_single_option_line("z_offset");

		optgroup = page->new_optgroup("Capabilities");
		ConfigOptionDef def;
			def.type =  coInt,
			def.default_value = new ConfigOptionInt(1); 
			def.label = "Extruders";
			def.tooltip = "Number of extruders of the printer.";
			def.min = 1;
		Option option(def, "extruders_count");
		optgroup->append_single_option_line(option);
		optgroup->append_single_option_line("single_extruder_multi_material");

// 		$optgroup->on_change(sub{
// 			my($opt_key, $value) = @_;
// 			wxTheApp->CallAfter(sub{
// 				if ($opt_key eq 'extruders_count') {
// 					$self->_extruders_count_changed($optgroup->get_value('extruders_count'));
// 					$self->update_dirty;
// 				}
// 				else {
// 					$self->update_dirty;
// 					$self->_on_value_change($opt_key, $value);
// 				}
// 			});
// 		});

		//if (!$params{ no_controller })
		if (!m_no_controller/*m_app_config->get("no_controller").empty()*/)
		{
		optgroup = page->new_optgroup("USB/Serial connection");
			line = {"Serial port", ""};
			Option serial_port = optgroup->get_option("serial_port");
			serial_port.side_widget = ([](wxWindow* parent){
				auto btn = new wxBitmapButton(parent, wxID_ANY, wxBitmap(wxString::FromUTF8(Slic3r::var("arrow_rotate_clockwise.png").c_str()), wxBITMAP_TYPE_PNG),
					wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
				btn->SetToolTip("Rescan serial ports");
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [](wxCommandEvent e) {/*_update_serial_ports*/; });
				return sizer;
			});
			Option serial_speed = optgroup->get_option("serial_speed");
			//! this serial_port & serial_speed have to be config !??
			auto serial_test = [this, serial_port, serial_speed](wxWindow* parent){
				auto btn = serial_test_btn = new wxButton(parent, wxID_ANY,
					"Test", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
//				btn->SetFont($Slic3r::GUI::small_font);
				btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("wrench.png").c_str()), wxBITMAP_TYPE_PNG));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [parent, serial_port, serial_speed](wxCommandEvent e){
					auto sender = new GCodeSender();					
					auto res = sender->connect(
						static_cast<const ConfigOptionString*>(serial_port.opt.default_value)->value,	//! m_config.serial_port,
						serial_speed.opt.default_value->getInt()										//! m_config.serial_speed
						);
					if (res && sender->wait_connected()) {
						show_info(parent, "Connection to printer works correctly.", "Success!");
					}
					else {
						show_error(parent, "Connection failed.");
					}
				});
				return sizer;
			};

			line.append_option(serial_port);
			line.append_option(serial_speed/*get_option("serial_speed")*/);
			line.append_widget(serial_test);
			optgroup->append_line(line);
		}

		optgroup = page->new_optgroup("OctoPrint upload");
		// # append two buttons to the Host line
		auto octoprint_host_browse = [] (wxWindow* parent) {
			auto btn = new wxButton(parent, wxID_ANY, "Browse…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
//			btn->SetFont($Slic3r::GUI::small_font);
			btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("zoom.png").c_str()), wxBITMAP_TYPE_PNG));
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

// 			if (!eval "use Net::Bonjour; 1") {
// 				btn->Disable;
// 			}

			btn->Bind(wxEVT_BUTTON, [parent](wxCommandEvent e){
				// # look for devices
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
					auto msg_window = new wxMessageDialog(parent, "No Bonjour device found", "Device Browser", wxOK | wxICON_INFORMATION);
					msg_window->ShowModal();
//				}
			});

			return sizer;
		};

		auto octoprint_host_test = [this](wxWindow* parent) {
			auto btn = octoprint_host_test_btn = new wxButton(parent, wxID_ANY,
				"Test", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
//			btn->SetFont($Slic3r::GUI::small_font);
			btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("wrench.png").c_str()), wxBITMAP_TYPE_PNG));
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, [parent](wxCommandEvent e) {
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

		optgroup = page->new_optgroup("Firmware");
		optgroup->append_single_option_line("gcode_flavor");

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line("use_relative_e_distances");
		optgroup->append_single_option_line("use_firmware_retraction");
		optgroup->append_single_option_line("use_volumetric_e");
		optgroup->append_single_option_line("variable_layer_height");

	page = add_options_page("Custom G-code", "cog.png");
		optgroup = page->new_optgroup("Start G-code", 0);
		option = optgroup->get_option("start_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("End G-code", 0);
		option = optgroup->get_option("end_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Before layer change G-code", 0);
		option = optgroup->get_option("before_layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("After layer change G-code", 0);
		option = optgroup->get_option("layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Tool change G-code", 0);
		option = optgroup->get_option("toolchange_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Between objects G-code (for sequential printing)", 0);
		option = optgroup->get_option("between_objects_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);
	
	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes", 0);
		option = optgroup->get_option("printer_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	build_extruder_pages();

// 	$self->_update_serial_ports if (!$params{ no_controller });
	if (!m_no_controller/*m_app_config->get("no_controller").empty()*/){
		Field *field = optgroup->get_field("serial_port");
		Choice *choice = static_cast<Choice *>(field);
		choice->set_values(scan_serial_ports());
	}
}

void TabPrinter::build_extruder_pages(){
//	auto default_config = m_preset_bundle->full_config();

	std::vector<PageShp>	extruder_pages;	

	for (auto extruder_idx = 0; extruder_idx < m_extruders_count; ++extruder_idx){
		//# build page
		auto page = add_options_page("Extruder " + wxString::Format(_T("%i"), extruder_idx + 1), "funnel.png", true);
		extruder_pages.push_back(page);
			
			auto optgroup = page->new_optgroup("Size");
			optgroup->append_single_option_line("nozzle_diameter", extruder_idx);
		
			optgroup = page->new_optgroup("Layer height limits");
			optgroup->append_single_option_line("min_layer_height", extruder_idx);
			optgroup->append_single_option_line("max_layer_height", extruder_idx);
				
		
			optgroup = page->new_optgroup("Position (for multi-extruder printers)");
			optgroup->append_single_option_line("extruder_offset", extruder_idx);
		
			optgroup = page->new_optgroup("Retraction");
			optgroup->append_single_option_line("retract_length", extruder_idx);
			optgroup->append_single_option_line("retract_lift", extruder_idx);
				Line line = { "Only lift Z", "" };
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
	
			optgroup = page->new_optgroup("Retraction when tool is disabled (advanced settings for multi-extruder setups)");
			optgroup->append_single_option_line("retract_length_toolchange", extruder_idx);
			optgroup->append_single_option_line("retract_restart_extra_toolchange", extruder_idx);

			optgroup = page->new_optgroup("Preview");
			optgroup->append_single_option_line("extruder_colour", extruder_idx);
	}
 
	// # remove extra pages
	if (m_extruders_count <= extruder_pages.size()) {
		extruder_pages.resize(m_extruders_count);
	}

	// # rebuild page list
	PageShp page_note = m_pages.back();
	m_pages.pop_back();
	for (auto page_extruder : extruder_pages)
		m_pages.push_back(page_extruder);
	m_pages.push_back(page_note);

	rebuild_page_tree();
}

void Tab::load_current_preset()
{
	;
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
	event.GetKeyCode() == WXK_TAB ?
		m_treectrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward) :
		event.Skip();
};

void Tab::save_preset(wxCommandEvent &event){};
void Tab::delete_preset(wxCommandEvent &event){};
void Tab::toggle_show_hide_incompatible(wxCommandEvent &event){};

//	# Return a callback to create a Tab widget to mark the preferences as compatible / incompatible to the current printer.
wxSizer* Tab::compatible_printers_widget(wxWindow* parent, wxCheckBox* checkbox, wxButton* btn)
{
// 	checkbox = new wxCheckBox(parent, wxID_ANY, "All");
// 
// 	btn = new wxButton(parent, wxID_ANY, "Set…", wxDefaultPosition, wxDefaultSize,
// 	wxBU_LEFT | wxBU_EXACTFIT);
//	btn->SetFont(GUI::small_font);
	btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("printer_empty.png").c_str()), wxBITMAP_TYPE_PNG));

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);

	checkbox->Bind(wxEVT_CHECKBOX, ([=](wxCommandEvent e)
	{
		btn->Enable(!checkbox->GetValue());
		// All printers have been made compatible with this preset.
		if (checkbox->GetValue())
			load_key_value("compatible_printers", std::vector<std::string> {});
		get_field("compatible_printers_condition")->toggle(checkbox->GetValue());
	}) );

	btn->Bind(wxEVT_BUTTON, ([this, parent, checkbox, btn](wxCommandEvent e)
	{
		// # Collect names of non-default non-external printer profiles.
		PresetCollection *printers = &m_preset_bundle->printers;//new PresetCollection(Preset::TYPE_PRINTER, Preset::print_options());
		wxArrayString presets;
		for (size_t idx = 0; idx < printers->size(); ++idx)
		{
			Preset& preset = printers->preset(idx);
			if (!preset.is_default && !preset.is_external)
				presets.Add(preset.name);
		}

		auto dlg = new wxMultiChoiceDialog(parent,
		"Select the printers this profile is compatible with.",
		"Compatible printers",  presets);
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
				checkbox->SetValue(1);
				btn->Disable();
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

// package Slic3r::GUI::Tab::Page;
ConfigOptionsGroupShp Page::new_optgroup(std::string title, int noncommon_label_width /*= -1*/)
{
	//! config_ have to be "right"
	ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(this, title, m_config);
	if (noncommon_label_width >= 0)
		optgroup->label_width = noncommon_label_width;

	optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value){
		//! This function will be called from OptionGroup.					
        wxTheApp->CallAfter([this, opt_key, value]() {
			static_cast<Tab*>(GetParent())->update_dirty();
			static_cast<Tab*>(GetParent())->on_value_change(opt_key, value);
        });
    },

	vsizer()->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 10);
	m_optgroups.push_back(optgroup);

	return optgroup;
}

} // GUI
} // Slic3r
