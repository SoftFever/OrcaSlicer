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
#include "../../libslic3r/Utils.hpp"

//#include "GCodeSender.hpp"

namespace Slic3r {
namespace GUI {

// sub new
void CTab::create_preset_tab(PresetBundle *preset_bundle, AppConfig *app_config)
{
	m_preset_bundle = preset_bundle;
	m_app_config = app_config;
	// Vertical sizer to hold the choice menu and the rest of the page.
	CTab *panel = this;
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

	m_treectrl->Bind(wxEVT_TREE_SEL_CHANGED, &CTab::OnTreeSelChange, this);
	m_treectrl->Bind(wxEVT_KEY_DOWN, &CTab::OnKeyDown, this);
	m_treectrl->Bind(wxEVT_COMBOBOX, &CTab::OnComboBox, this); 

	m_btn_save_preset->Bind(wxEVT_BUTTON, &CTab::save_preset, this);
	m_btn_delete_preset->Bind(wxEVT_BUTTON, &CTab::delete_preset, this);
	m_btn_hide_incompatible_presets->Bind(wxEVT_BUTTON, &CTab::toggle_show_hide_incompatible, this);

	// Initialize the DynamicPrintConfig by default keys/values.
	// Possible %params keys: no_controller
	build();
	rebuild_page_tree();
//	_update();
}

CPageShp CTab::add_options_page(wxString title, std::string icon, bool is_extruder_pages/* = false*/)
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
	CPageShp page(new CPage(this, title, icon_idx));
	page->SetScrollbars(1, 1, 1, 1);
	page->Hide();
	m_hsizer->Add(page.get(), 1, wxEXPAND | wxLEFT, 5);
	if (!is_extruder_pages) 
		m_pages.push_back(page);

	page->set_config(&m_config);
	return page;
}

void CTab::load_key_value(std::string opt_key, std::vector<std::string> value)
{
	// # To be called by custom widgets, load a value into a config,
	// # update the preset selection boxes (the dirty flags)
	// $self->{config}->set($opt_key, $value);
	// # Mark the print & filament enabled if they are compatible with the currently selected preset.
	if (opt_key.compare("compatible_printers") == 0) {
		// wxTheApp->{preset_bundle}->update_compatible_with_printer(0);
		// $self->{presets}->update_tab_ui($self->{presets_choice}, $self->{show_incompatible_presets});
		// } else {
		// $self->{presets}->update_dirty_ui($self->{presets_choice});
	}
	// $self->_on_presets_changed;
	// $self->_update;
}

void CTabPrint::build()
{
	m_config = m_preset_bundle->prints.get_edited_preset().config;
	m_config_def = m_config.def();

	auto page = add_options_page("Layers and perimeters", "layers.png");
		auto optgroup = page->new_optgroup("Layer height");
		optgroup->append_single_option_line(get_option("layer_height"));
		optgroup->append_single_option_line(get_option("first_layer_height"));

		optgroup = page->new_optgroup("Vertical shells");
		optgroup->append_single_option_line(get_option("perimeters"));
		optgroup->append_single_option_line(get_option("spiral_vase"));

		optgroup = page->new_optgroup("Horizontal shells");
		Line line{ "Solid layers", "" };
		line.append_option(get_option("top_solid_layers"));
		line.append_option(get_option("bottom_solid_layers"));
		optgroup->append_line(line);

		optgroup = page->new_optgroup("Quality (slower slicing)");
		optgroup->append_single_option_line(get_option("extra_perimeters"));
		optgroup->append_single_option_line(get_option("ensure_vertical_shell_thickness"));
		optgroup->append_single_option_line(get_option("avoid_crossing_perimeters"));
		optgroup->append_single_option_line(get_option("thin_walls"));
		optgroup->append_single_option_line(get_option("overhangs"));

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line(get_option("seam_position"));
		optgroup->append_single_option_line(get_option("external_perimeters_first"));

	page = add_options_page("Infill", "infill.png");
		optgroup = page->new_optgroup("Infill");
		optgroup->append_single_option_line(get_option("fill_density"));
		optgroup->append_single_option_line(get_option("fill_pattern"));
		optgroup->append_single_option_line(get_option("external_fill_pattern"));

		optgroup = page->new_optgroup("Reducing printing time");
		optgroup->append_single_option_line(get_option("infill_every_layers"));
		optgroup->append_single_option_line(get_option("infill_only_where_needed"));

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line(get_option("solid_infill_every_layers"));
		optgroup->append_single_option_line(get_option("fill_angle"));
		optgroup->append_single_option_line(get_option("solid_infill_below_area"));
		optgroup->append_single_option_line(get_option("bridge_angle"));
		optgroup->append_single_option_line(get_option("only_retract_when_crossing_perimeters"));
		optgroup->append_single_option_line(get_option("infill_first"));

	page = add_options_page("Skirt and brim", "box.png");
		optgroup = page->new_optgroup("Skirt");
		optgroup->append_single_option_line(get_option("skirts"));
		optgroup->append_single_option_line(get_option("skirt_distance"));
		optgroup->append_single_option_line(get_option("skirt_height"));
		optgroup->append_single_option_line(get_option("min_skirt_length"));

		optgroup = page->new_optgroup("Brim");
		optgroup->append_single_option_line(get_option("brim_width"));

	page = add_options_page("Support material", "building.png");
		optgroup = page->new_optgroup("Support material");
		optgroup->append_single_option_line(get_option("support_material"));
		optgroup->append_single_option_line(get_option("support_material_threshold"));
		optgroup->append_single_option_line(get_option("support_material_enforce_layers"));

		optgroup = page->new_optgroup("Raft");
		optgroup->append_single_option_line(get_option("raft_layers"));
//		# optgroup->append_single_option_line(get_option_("raft_contact_distance"));

		optgroup = page->new_optgroup("Options for support material and raft");
		optgroup->append_single_option_line(get_option("support_material_contact_distance"));
		optgroup->append_single_option_line(get_option("support_material_pattern"));
		optgroup->append_single_option_line(get_option("support_material_with_sheath"));
		optgroup->append_single_option_line(get_option("support_material_spacing"));
		optgroup->append_single_option_line(get_option("support_material_angle"));
		optgroup->append_single_option_line(get_option("support_material_interface_layers"));
		optgroup->append_single_option_line(get_option("support_material_interface_spacing"));
		optgroup->append_single_option_line(get_option("support_material_interface_contact_loops"));
		optgroup->append_single_option_line(get_option("support_material_buildplate_only"));
		optgroup->append_single_option_line(get_option("support_material_xy_spacing"));
		optgroup->append_single_option_line(get_option("dont_support_bridges"));
		optgroup->append_single_option_line(get_option("support_material_synchronize_layers"));

	page = add_options_page("Speed", "time.png");
		optgroup = page->new_optgroup("Speed for print moves");
		optgroup->append_single_option_line(get_option("perimeter_speed"));
		optgroup->append_single_option_line(get_option("small_perimeter_speed"));
		optgroup->append_single_option_line(get_option("external_perimeter_speed"));
		optgroup->append_single_option_line(get_option("infill_speed"));
		optgroup->append_single_option_line(get_option("solid_infill_speed"));
		optgroup->append_single_option_line(get_option("top_solid_infill_speed"));
		optgroup->append_single_option_line(get_option("support_material_speed"));
		optgroup->append_single_option_line(get_option("support_material_interface_speed"));
		optgroup->append_single_option_line(get_option("bridge_speed"));
		optgroup->append_single_option_line(get_option("gap_fill_speed"));

		optgroup = page->new_optgroup("Speed for non-print moves");
		optgroup->append_single_option_line(get_option("travel_speed"));

		optgroup = page->new_optgroup("Modifiers");
		optgroup->append_single_option_line(get_option("first_layer_speed"));

		optgroup = page->new_optgroup("Acceleration control (advanced)");
		optgroup->append_single_option_line(get_option("perimeter_acceleration"));
		optgroup->append_single_option_line(get_option("infill_acceleration"));
		optgroup->append_single_option_line(get_option("bridge_acceleration"));
		optgroup->append_single_option_line(get_option("first_layer_acceleration"));
		optgroup->append_single_option_line(get_option("default_acceleration"));

		optgroup = page->new_optgroup("Autospeed (advanced)");
		optgroup->append_single_option_line(get_option("max_print_speed"));
		optgroup->append_single_option_line(get_option("max_volumetric_speed"));
		optgroup->append_single_option_line(get_option("max_volumetric_extrusion_rate_slope_positive"));
		optgroup->append_single_option_line(get_option("max_volumetric_extrusion_rate_slope_negative"));

	page = add_options_page("Multiple Extruders", "funnel.png");
		optgroup = page->new_optgroup("Extruders");
		optgroup->append_single_option_line(get_option("perimeter_extruder"));
		optgroup->append_single_option_line(get_option("infill_extruder"));
		optgroup->append_single_option_line(get_option("solid_infill_extruder"));
		optgroup->append_single_option_line(get_option("support_material_extruder"));
		optgroup->append_single_option_line(get_option("support_material_interface_extruder"));

		optgroup = page->new_optgroup("Ooze prevention");
		optgroup->append_single_option_line(get_option("ooze_prevention"));
		optgroup->append_single_option_line(get_option("standby_temperature_delta"));

		optgroup = page->new_optgroup("Wipe tower");
		optgroup->append_single_option_line(get_option("wipe_tower"));
		optgroup->append_single_option_line(get_option("wipe_tower_x"));
		optgroup->append_single_option_line(get_option("wipe_tower_y"));
		optgroup->append_single_option_line(get_option("wipe_tower_width"));
		optgroup->append_single_option_line(get_option("wipe_tower_per_color_wipe"));

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line(get_option("interface_shells"));

	page = add_options_page("Advanced", "wrench.png");
		optgroup = page->new_optgroup("Extrusion width", 180);
		optgroup->append_single_option_line(get_option("extrusion_width"));
		optgroup->append_single_option_line(get_option("first_layer_extrusion_width"));
		optgroup->append_single_option_line(get_option("perimeter_extrusion_width"));
		optgroup->append_single_option_line(get_option("external_perimeter_extrusion_width"));
		optgroup->append_single_option_line(get_option("infill_extrusion_width"));
		optgroup->append_single_option_line(get_option("solid_infill_extrusion_width"));
		optgroup->append_single_option_line(get_option("top_infill_extrusion_width"));
		optgroup->append_single_option_line(get_option("support_material_extrusion_width"));

		optgroup = page->new_optgroup("Overlap");
		optgroup->append_single_option_line(get_option("infill_overlap"));

		optgroup = page->new_optgroup("Flow");
		optgroup->append_single_option_line(get_option("bridge_flow_ratio"));

		optgroup = page->new_optgroup("Other");
		optgroup->append_single_option_line(get_option("clip_multipart_objects"));
		optgroup->append_single_option_line(get_option("elefant_foot_compensation"));
		optgroup->append_single_option_line(get_option("xy_size_compensation"));
//		#            optgroup->append_single_option_line(get_option_("threads"));
		optgroup->append_single_option_line(get_option("resolution"));

	page = add_options_page("Output options", "page_white_go.png");
		optgroup = page->new_optgroup("Sequential printing");
		optgroup->append_single_option_line(get_option("complete_objects"));
		line = Line{ "Extruder clearance (mm)", "" };
		Option option = get_option("extruder_clearance_radius");
		option.opt.width = 60;
		line.append_option(option);
		option = get_option("extruder_clearance_height");
		option.opt.width = 60;
		line.append_option(option);
		optgroup->append_line(line);

		optgroup = page->new_optgroup("Output file");
		optgroup->append_single_option_line(get_option("gcode_comments"));
		option = get_option("output_filename_format");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Post-processing scripts", 0);	
		option = get_option("post_process");
		option.opt.full_width = true;
		option.opt.height = 50;
		optgroup->append_single_option_line(option);

	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes", 0);						
		option = get_option("notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page("Dependencies", "wrench.png");
		optgroup = page->new_optgroup("Profile dependencies");
		line = Line{ "Compatible printers", "" };
		line.widget = [this](wxWindow* parent){
			return compatible_printers_widget(parent, m_compatible_printers_checkbox, m_compatible_printers_btn);
		};
		optgroup->append_line(line);
}

void CTabFilament::build()
{
	m_config = m_preset_bundle->filaments.get_edited_preset().config;
	m_config_def = m_config.def();

	auto page = add_options_page("Filament", "spool.png");
		auto optgroup = page->new_optgroup("Filament");
		optgroup->append_single_option_line(get_option("filament_colour"));
		optgroup->append_single_option_line(get_option("filament_diameter"));
		optgroup->append_single_option_line(get_option("extrusion_multiplier"));
		optgroup->append_single_option_line(get_option("filament_density"));
		optgroup->append_single_option_line(get_option("filament_cost"));

		optgroup = page->new_optgroup("Temperature (°C)");
		Line line = { "Extruder", "" };
		line.append_option(get_option("first_layer_temperature"));
		line.append_option(get_option("temperature"));
		optgroup->append_line(line);

		line = { "Bed", "" };
		line.append_option(get_option("first_layer_bed_temperature"));
		line.append_option(get_option("bed_temperature"));
		optgroup->append_line(line);

	page = add_options_page("Cooling", "hourglass.png");
		optgroup = page->new_optgroup("Enable");
		optgroup->append_single_option_line(get_option("fan_always_on"));
		optgroup->append_single_option_line(get_option("cooling"));

		line = { "", "" }; 
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, m_cooling_description_line);
		};
		optgroup->append_line(line);

		optgroup = page->new_optgroup("Fan settings");
		line = {"Fan speed",""};
		line.append_option(get_option("min_fan_speed"));
		line.append_option(get_option("max_fan_speed"));
		optgroup->append_line(line);

		optgroup->append_single_option_line(get_option("bridge_fan_speed"));
		optgroup->append_single_option_line(get_option("disable_fan_first_layers"));

		optgroup = page->new_optgroup("Cooling thresholds", 250);
		optgroup->append_single_option_line(get_option("fan_below_layer_time"));
		optgroup->append_single_option_line(get_option("slowdown_below_layer_time"));
		optgroup->append_single_option_line(get_option("min_print_speed"));

	page = add_options_page("Advanced", "wrench.png");
		optgroup = page->new_optgroup("Filament properties");
		optgroup->append_single_option_line(get_option("filament_type"));
		optgroup->append_single_option_line(get_option("filament_soluble"));

		optgroup = page->new_optgroup("Print speed override");
		optgroup->append_single_option_line(get_option("filament_max_volumetric_speed"));

		line = {"",""};
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, m_volumetric_speed_description_line);
		};
		optgroup->append_line(line);

	page = add_options_page("Custom G-code", "cog.png");
		optgroup = page->new_optgroup("Start G-code", 0);
		Option option = get_option("start_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("End G-code", 0);
		option = get_option("end_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes", 0);
		optgroup->label_width = 0;
		option = get_option("filament_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page("Dependencies", "wrench.png");
		optgroup = page->new_optgroup("Profile dependencies");
		line = {"Compatible printers", ""};
		line.widget = [this](wxWindow* parent){
			return compatible_printers_widget(parent, m_compatible_printers_checkbox, m_compatible_printers_btn);
		};
		optgroup->append_line(line);
}

wxSizer* CTabFilament::description_line_widget(wxWindow* parent, wxStaticText* StaticText)
{
	StaticText = new wxStaticText(parent, wxID_ANY, "gfghjkkl;\n fgdsufhsreotklg\n iesrftorsikgyfkh\nauiwrhfidj", wxDefaultPosition, wxDefaultSize);
	auto font = (new wxSystemSettings)->GetFont(wxSYS_DEFAULT_GUI_FONT);
	StaticText->SetFont(font);
	StaticText->Wrap(400);
	StaticText->GetParent()->Layout();

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(StaticText);
	return sizer;
}

void CTabPrinter::build()
{
	m_config = m_preset_bundle->printers.get_edited_preset().config;
	m_config_def = m_config.def();		// It will be used in get_option_(const std::string title)
	auto default_config = m_preset_bundle->full_config();

	auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config.option("nozzle_diameter"));
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
		optgroup->append_single_option_line(get_option("z_offset"));

		optgroup = page->new_optgroup("Capabilities");
		ConfigOptionDef def;
			def.type =  coInt,
			def.default_value = new ConfigOptionInt(1); 
			def.label = "Extruders";
			def.tooltip = "Number of extruders of the printer.";
			def.min = 1;
		Option option(def, "extruders_count");
		optgroup->append_single_option_line(option);
		optgroup->append_single_option_line(get_option("single_extruder_multi_material"));

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
		if (m_app_config->get("no_controller").empty())
		{
		optgroup = page->new_optgroup("USB/Serial connection");
			line = {"Serial port", ""};
			Option serial_port = get_option("serial_port");
			serial_port.side_widget = ([](wxWindow* parent){
				auto btn = new wxBitmapButton(parent, wxID_ANY, wxBitmap(wxString::FromUTF8(Slic3r::var("arrow_rotate_clockwise.png").c_str()), wxBITMAP_TYPE_PNG),
					wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
				btn->SetToolTip("Rescan serial ports");
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [](wxCommandEvent e) {/*_update_serial_ports*/; });
				return sizer;
			});
			Option serial_speed = get_option("serial_speed");
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

		Line host_line = optgroup->create_single_option_line(get_option("octoprint_host"));
		host_line.append_widget(octoprint_host_browse);
		host_line.append_widget(octoprint_host_test);
		optgroup->append_line(host_line);
		optgroup->append_single_option_line(get_option("octoprint_apikey"));

		optgroup = page->new_optgroup("Firmware");
		optgroup->append_single_option_line(get_option("gcode_flavor"));

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line(get_option("use_relative_e_distances"));
		optgroup->append_single_option_line(get_option("use_firmware_retraction"));
		optgroup->append_single_option_line(get_option("use_volumetric_e"));
		optgroup->append_single_option_line(get_option("variable_layer_height"));

	page = add_options_page("Custom G-code", "cog.png");
		optgroup = page->new_optgroup("Start G-code", 0);
		option = get_option("start_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("End G-code", 0);
		option = get_option("end_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Before layer change G-code", 0);
		option = get_option("before_layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("After layer change G-code", 0);
		option = get_option("layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Tool change G-code", 0);
		option = get_option("toolchange_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Between objects G-code (for sequential printing)", 0);
		option = get_option("between_objects_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);
	
	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes", 0);
		option = get_option("printer_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	build_extruder_pages();

// 	$self->_update_serial_ports if (!$params{ no_controller });
	if (m_app_config->get("no_controller").empty()){
		Field *field = optgroup->get_field("serial_port");
		Choice *choice = static_cast<Choice *>(field);
		choice->set_values(scan_serial_ports());
	}
}

void CTabPrinter::build_extruder_pages(){
//	auto default_config = m_preset_bundle->full_config();

	std::vector<CPageShp>	extruder_pages;	

	for (auto extruder_idx = 0; extruder_idx < m_extruders_count; ++extruder_idx){
		//# build page
		auto page = add_options_page("Extruder " + wxString::Format(_T("%i"), extruder_idx + 1), "funnel.png", true);
		extruder_pages.push_back(page);
			
			auto optgroup = page->new_optgroup("Size");
			optgroup->append_single_option_line(get_option("nozzle_diameter", extruder_idx));
		
			optgroup = page->new_optgroup("Layer height limits");
			optgroup->append_single_option_line(get_option("min_layer_height", extruder_idx));
			optgroup->append_single_option_line(get_option("max_layer_height", extruder_idx));
				
		
			optgroup = page->new_optgroup("Position (for multi-extruder printers)");
			optgroup->append_single_option_line(get_option("extruder_offset", extruder_idx));
		
			optgroup = page->new_optgroup("Retraction");
			optgroup->append_single_option_line(get_option("retract_length", extruder_idx));
			optgroup->append_single_option_line(get_option("retract_lift", extruder_idx));
				Line line = { "Only lift Z", "" };
				line.append_option(get_option("retract_lift_above", extruder_idx));
				line.append_option(get_option("retract_lift_below", extruder_idx));
				optgroup->append_line(line);
			
			optgroup->append_single_option_line(get_option("retract_speed", extruder_idx));
			optgroup->append_single_option_line(get_option("deretract_speed", extruder_idx));
			optgroup->append_single_option_line(get_option("retract_restart_extra", extruder_idx));
			optgroup->append_single_option_line(get_option("retract_before_travel", extruder_idx));
			optgroup->append_single_option_line(get_option("retract_layer_change", extruder_idx));
			optgroup->append_single_option_line(get_option("wipe", extruder_idx));
			optgroup->append_single_option_line(get_option("retract_before_wipe", extruder_idx));
	
			optgroup = page->new_optgroup("Retraction when tool is disabled (advanced settings for multi-extruder setups)");
			optgroup->append_single_option_line(get_option("retract_length_toolchange", extruder_idx));
			optgroup->append_single_option_line(get_option("retract_restart_extra_toolchange", extruder_idx));

			optgroup = page->new_optgroup("Preview");
			optgroup->append_single_option_line(get_option("extruder_colour", extruder_idx));
	}
 
	// # remove extra pages
	if (m_extruders_count <= extruder_pages.size()) {
		extruder_pages.resize(m_extruders_count);
	}

	// # rebuild page list
	CPageShp page_note = m_pages.back();
	m_pages.pop_back();
	for (auto page_extruder : extruder_pages)
		m_pages.push_back(page_extruder);
	m_pages.push_back(page_note);

	rebuild_page_tree();
}

//Regerenerate content of the page tree.
void CTab::rebuild_page_tree()
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

void CTab::OnTreeSelChange(wxTreeEvent& event)
{
	if (m_disable_tree_sel_changed_event) return;
	CPage* page = nullptr;
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

void CTab::OnKeyDown(wxKeyEvent& event)
{
	event.GetKeyCode() == WXK_TAB ?
		m_treectrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward) :
		event.Skip();
};

void CTab::save_preset(wxCommandEvent &event){};
void CTab::delete_preset(wxCommandEvent &event){};
void CTab::toggle_show_hide_incompatible(wxCommandEvent &event){};

//	# Return a callback to create a Tab widget to mark the preferences as compatible / incompatible to the current printer.
wxSizer* CTab::compatible_printers_widget(wxWindow* parent, wxCheckBox* checkbox, wxButton* btn)
{
	checkbox = new wxCheckBox(parent, wxID_ANY, "All");

	btn = new wxButton(parent, wxID_ANY, "Set…", wxDefaultPosition, wxDefaultSize,
	wxBU_LEFT | wxBU_EXACTFIT);
//	btn->SetFont(GUI::small_font);
	btn->SetBitmap(wxBitmap(wxString::FromUTF8(Slic3r::var("printer_empty.png").c_str()), wxBITMAP_TYPE_PNG));

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);

	checkbox->Bind(wxEVT_CHECKBOX, ([=](wxCommandEvent e)
	{
		 btn->Enable(!checkbox->GetValue());
	// # All printers have been made compatible with this preset.
	//	 _load_key_value('compatible_printers', []) if checkbox->GetValue();
	}) );

	btn->Bind(wxEVT_BUTTON, ([=](wxCommandEvent e)
	{
		PresetCollection *prints = new PresetCollection(Preset::TYPE_PRINT, Preset::print_options());
		prints->preset(0).config.opt_string("print_settings_id", true);
		prints->preset(0).config.optptr("compatible_printers", true);
		DynamicPrintConfig config_ = prints->get_edited_preset().config;

		// # Collect names of non-default non-external printer profiles.
		PresetCollection *printers = new PresetCollection(Preset::TYPE_PRINTER, Preset::print_options());
		printers->preset(0).config.opt_string("print_settings_id", true);
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
		auto *compatible_printers = dynamic_cast<const ConfigOptionStrings*>(config_.option("compatible_printers"));
		if (compatible_printers != nullptr || !compatible_printers->values.empty())
			for (auto preset_name : compatible_printers->values)
				for (size_t idx = 0; idx < presets.GetCount(); ++idx)
					if (presets[idx].compare(preset_name) == 0)
					{
						selections.Add(idx);
						break;
					}
		dlg->SetSelections(selections);
		// # Show the dialog.
		if (dlg->ShowModal() == wxID_OK) {
			selections.Clear();
			selections = dlg->GetSelections();
			std::vector<std::string> value;
			for (auto idx : selections)
				value.push_back(presets[idx].ToStdString());
			if (/*!@$value*/value.empty()) {
				checkbox->SetValue(1);
				btn->Disable();
			}
		// # All printers have been made compatible with this preset.
		// _load_key_value('compatible_printers', $value);
		}
	}));
	return sizer; 
}

// package Slic3r::GUI::Tab::Page;
ConfigOptionsGroupShp CPage::new_optgroup(std::string title, int noncommon_label_width /*= -1*/)
{
	//! config_ have to be "right"
	ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(this, title, m_config);
	if (noncommon_label_width >= 0)
		optgroup->label_width = noncommon_label_width;

//         on_change       => sub {
//             my ($opt_key, $value) = @_;
//             wxTheApp->CallAfter(sub {
//                 $self->GetParent->update_dirty;
//                 $self->GetParent->_on_value_change($opt_key, $value);
//             });
//         },

	vsizer()->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 10);
	m_optgroups.push_back(optgroup);

	return optgroup;
}

} // GUI
} // Slic3r
