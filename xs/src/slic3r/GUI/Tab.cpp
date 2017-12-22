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
//#include "GCodeSender.hpp"

namespace Slic3r {
namespace GUI {

// sub new
void CTab::create_preset_tab(PresetBundle *preset_bundle)
{
	preset_bundle_ = preset_bundle;
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

	presets_choice_ = new wxBitmapComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(270, -1)/*, nCntEl, choices, wxCB_READONLY*/);
	const wxBitmap* bmp = new wxBitmap(wxT("var\\flag-green-icon.png"), wxBITMAP_TYPE_PNG);
	for (auto el:choices)
		presets_choice_->Append(wxString::FromUTF8(el).c_str(), *bmp);
	presets_choice_->SetSelection(presets_choice_->GetCount() - 1);

	//buttons
	wxBitmap bmpMenu;
	bmpMenu = wxBitmap(wxT("var\\disk.png"), wxBITMAP_TYPE_PNG);
	btn_save_preset_ = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	bmpMenu = wxBitmap(wxT("var\\delete.png"), wxBITMAP_TYPE_PNG);
	btn_delete_preset_ = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	//	$self->{show_incompatible_presets} = 0;			// !!!

	bmp_show_incompatible_presets_ = new wxBitmap(wxT("var\\flag-red-icon.png"), wxBITMAP_TYPE_PNG);
	bmp_hide_incompatible_presets_ = new wxBitmap(wxT("var\\flag-green-icon.png"), wxBITMAP_TYPE_PNG);
	btn_hide_incompatible_presets_ = new wxBitmapButton(panel, wxID_ANY, *bmp_hide_incompatible_presets_, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	btn_save_preset_->SetToolTip(wxT("Save current ") + wxString(title_));// (stTitle);
	btn_delete_preset_->SetToolTip(_T("Delete this preset"));
	btn_delete_preset_->Disable();

	hsizer_ = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(hsizer_, 0, wxBOTTOM, 3);
	hsizer_->Add(presets_choice_, 1, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
	hsizer_->AddSpacer(4);
	hsizer_->Add(btn_save_preset_, 0, wxALIGN_CENTER_VERTICAL);
	hsizer_->AddSpacer(4);
	hsizer_->Add(btn_delete_preset_, 0, wxALIGN_CENTER_VERTICAL);
	hsizer_->AddSpacer(16);
	hsizer_->Add(btn_hide_incompatible_presets_, 0, wxALIGN_CENTER_VERTICAL);

	//Horizontal sizer to hold the tree and the selected page.
	hsizer_ = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(hsizer_, 1, wxEXPAND, 0);

	//left vertical sizer
	left_sizer_ = new wxBoxSizer(wxVERTICAL);
	hsizer_->Add(left_sizer_, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);

	// tree
	treectrl_ = new wxTreeCtrl(panel, wxID_ANY/*ID_TAB_TREE*/, wxDefaultPosition, wxSize(185, -1), wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN | wxWANTS_CHARS);
	left_sizer_->Add(treectrl_, 1, wxEXPAND);
	icons_ = new wxImageList(16, 16, true, 1/*, 1*/);
	// Index of the last icon inserted into $self->{icons}.
	icon_count = -1;
	treectrl_->AssignImageList(icons_);
	treectrl_->AddRoot("root");
	treectrl_->SetIndent(0);
	disable_tree_sel_changed_event_ = 0;

	treectrl_->Bind(wxEVT_TREE_SEL_CHANGED, &CTab::OnTreeSelChange, this);
	treectrl_->Bind(wxEVT_KEY_DOWN, &CTab::OnKeyDown, this);
	treectrl_->Bind(wxEVT_COMBOBOX, &CTab::OnComboBox, this); 

	btn_save_preset_->Bind(wxEVT_BUTTON, &CTab::save_preset, this);
	btn_delete_preset_->Bind(wxEVT_BUTTON, &CTab::delete_preset, this);
	btn_hide_incompatible_presets_->Bind(wxEVT_BUTTON, &CTab::_toggle_show_hide_incompatible, this);

	// Initialize the DynamicPrintConfig by default keys/values.
	// Possible %params keys: no_controller
	build();
	rebuild_page_tree();
//	_update();
}

CPageShp CTab::add_options_page(wxString title, wxString icon)
{
	// Index of icon in an icon list $self->{icons}.
	auto icon_idx = 0;
	if (!icon.IsEmpty()) {
		try { icon_idx = icon_index_.at(icon);}
		catch (std::out_of_range e) { icon_idx = -1; }
		if (icon_idx == -1) {
			// Add a new icon to the icon list.
			const auto img_icon = new wxIcon(wxT("var\\") + icon, wxBITMAP_TYPE_PNG);
			icons_->Add(*img_icon);
			icon_idx = ++icon_count; //  $icon_idx = $self->{icon_count} + 1; $self->{icon_count} = $icon_idx;
			icon_index_[icon] = icon_idx;
		}
	}
	// Initialize the page.
	CPageShp page(new CPage(this, title, icon_idx));
	page->SetScrollbars(1, 1, 1, 1);
	page->Hide();
	hsizer_->Add(page.get(), 1, wxEXPAND | wxLEFT, 5);
	pages_.push_back(page);

	page->set_config(&config_);

	return page;
}

void CTab::load_key_value_(std::string opt_key, std::vector<std::string> value)
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
	config_ = preset_bundle_->prints.get_edited_preset().config;
	config_def_ = config_.def();

	auto page = add_options_page("Layers and perimeters", "layers.png");
		auto optgroup = page->new_optgroup("Layer height");
		optgroup->append_single_option_line(get_option_("layer_height"));
		optgroup->append_single_option_line(get_option_("first_layer_height"));

		optgroup = page->new_optgroup("Vertical shells");
		optgroup->append_single_option_line(get_option_("perimeters"));
		optgroup->append_single_option_line(get_option_("spiral_vase"));

		optgroup = page->new_optgroup("Horizontal shells");
		Line line{ "Solid layers", "" };
		line.append_option(get_option_("top_solid_layers"));
		line.append_option(get_option_("bottom_solid_layers"));
		optgroup->append_line(line);

		optgroup = page->new_optgroup("Quality (slower slicing)");
 		optgroup->append_single_option_line(get_option_("extra_perimeters"));
 		optgroup->append_single_option_line(get_option_("ensure_vertical_shell_thickness"));
 		optgroup->append_single_option_line(get_option_("avoid_crossing_perimeters"));
 		optgroup->append_single_option_line(get_option_("thin_walls"));
		optgroup->append_single_option_line(get_option_("overhangs"));

		optgroup = page->new_optgroup("Advanced");
 		optgroup->append_single_option_line(get_option_("seam_position"));
 		optgroup->append_single_option_line(get_option_("external_perimeters_first"));

	page = add_options_page("Infill", "infill.png");
		optgroup = page->new_optgroup("Infill");
 		optgroup->append_single_option_line(get_option_("fill_density"));
		optgroup->append_single_option_line(get_option_("fill_pattern"));
		optgroup->append_single_option_line(get_option_("external_fill_pattern"));

		optgroup = page->new_optgroup("Reducing printing time");
 		optgroup->append_single_option_line(get_option_("infill_every_layers"));
 		optgroup->append_single_option_line(get_option_("infill_only_where_needed"));

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line(get_option_("solid_infill_every_layers"));
		optgroup->append_single_option_line(get_option_("fill_angle"));
		optgroup->append_single_option_line(get_option_("solid_infill_below_area"));
		optgroup->append_single_option_line(get_option_("bridge_angle"));
 		optgroup->append_single_option_line(get_option_("only_retract_when_crossing_perimeters"));
 		optgroup->append_single_option_line(get_option_("infill_first"));

	page = add_options_page("Skirt and brim", "box.png");
		optgroup = page->new_optgroup("Skirt");
		optgroup->append_single_option_line(get_option_("skirts"));
		optgroup->append_single_option_line(get_option_("skirt_distance"));
		optgroup->append_single_option_line(get_option_("skirt_height"));
		optgroup->append_single_option_line(get_option_("min_skirt_length"));

		optgroup = page->new_optgroup("Brim");
		optgroup->append_single_option_line(get_option_("brim_width"));

	page = add_options_page("Support material", "building.png");
		optgroup = page->new_optgroup("Support material");
 		optgroup->append_single_option_line(get_option_("support_material"));
		optgroup->append_single_option_line(get_option_("support_material_threshold"));
		optgroup->append_single_option_line(get_option_("support_material_enforce_layers"));

		optgroup = page->new_optgroup("Raft");
 		optgroup->append_single_option_line(get_option_("raft_layers"));
// 		# optgroup->append_single_option_line(get_option_("raft_contact_distance"));

		optgroup = page->new_optgroup("Options for support material and raft");
 		optgroup->append_single_option_line(get_option_("support_material_contact_distance"));
		optgroup->append_single_option_line(get_option_("support_material_pattern"));
 		optgroup->append_single_option_line(get_option_("support_material_with_sheath"));
		optgroup->append_single_option_line(get_option_("support_material_spacing"));
		optgroup->append_single_option_line(get_option_("support_material_angle"));
		optgroup->append_single_option_line(get_option_("support_material_interface_layers"));
		optgroup->append_single_option_line(get_option_("support_material_interface_spacing"));
		optgroup->append_single_option_line(get_option_("support_material_interface_contact_loops"));
 		optgroup->append_single_option_line(get_option_("support_material_buildplate_only"));
		optgroup->append_single_option_line(get_option_("support_material_xy_spacing"));
 		optgroup->append_single_option_line(get_option_("dont_support_bridges"));
 		optgroup->append_single_option_line(get_option_("support_material_synchronize_layers"));

	page = add_options_page("Speed", "time.png");
		optgroup = page->new_optgroup("Speed for print moves");
		optgroup->append_single_option_line(get_option_("perimeter_speed"));
		optgroup->append_single_option_line(get_option_("small_perimeter_speed"));
		optgroup->append_single_option_line(get_option_("external_perimeter_speed"));
		optgroup->append_single_option_line(get_option_("infill_speed"));
		optgroup->append_single_option_line(get_option_("solid_infill_speed"));
		optgroup->append_single_option_line(get_option_("top_solid_infill_speed"));
		optgroup->append_single_option_line(get_option_("support_material_speed"));
		optgroup->append_single_option_line(get_option_("support_material_interface_speed"));
		optgroup->append_single_option_line(get_option_("bridge_speed"));
		optgroup->append_single_option_line(get_option_("gap_fill_speed"));

		optgroup = page->new_optgroup("Speed for non-print moves");
		optgroup->append_single_option_line(get_option_("travel_speed"));

		optgroup = page->new_optgroup("Modifiers");
		optgroup->append_single_option_line(get_option_("first_layer_speed"));

		optgroup = page->new_optgroup("Acceleration control (advanced)");
		optgroup->append_single_option_line(get_option_("perimeter_acceleration"));
		optgroup->append_single_option_line(get_option_("infill_acceleration"));
		optgroup->append_single_option_line(get_option_("bridge_acceleration"));
		optgroup->append_single_option_line(get_option_("first_layer_acceleration"));
		optgroup->append_single_option_line(get_option_("default_acceleration"));

		optgroup = page->new_optgroup("Autospeed (advanced)");
		optgroup->append_single_option_line(get_option_("max_print_speed"));
		optgroup->append_single_option_line(get_option_("max_volumetric_speed"));
		optgroup->append_single_option_line(get_option_("max_volumetric_extrusion_rate_slope_positive"));
		optgroup->append_single_option_line(get_option_("max_volumetric_extrusion_rate_slope_negative"));

	page = add_options_page("Multiple Extruders", "funnel.png");
		optgroup = page->new_optgroup("Extruders");
 		optgroup->append_single_option_line(get_option_("perimeter_extruder"));
 		optgroup->append_single_option_line(get_option_("infill_extruder"));
		optgroup->append_single_option_line(get_option_("solid_infill_extruder"));
		optgroup->append_single_option_line(get_option_("support_material_extruder"));
		optgroup->append_single_option_line(get_option_("support_material_interface_extruder"));

		optgroup = page->new_optgroup("Ooze prevention");
 		optgroup->append_single_option_line(get_option_("ooze_prevention"));
		optgroup->append_single_option_line(get_option_("standby_temperature_delta"));

		optgroup = page->new_optgroup("Wipe tower");
 		optgroup->append_single_option_line(get_option_("wipe_tower"));
		optgroup->append_single_option_line(get_option_("wipe_tower_x"));
		optgroup->append_single_option_line(get_option_("wipe_tower_y"));
		optgroup->append_single_option_line(get_option_("wipe_tower_width"));
		optgroup->append_single_option_line(get_option_("wipe_tower_per_color_wipe"));

		optgroup = page->new_optgroup("Advanced");
 		optgroup->append_single_option_line(get_option_("interface_shells"));

	page = add_options_page("Advanced", "wrench.png");
		optgroup = page->new_optgroup("Extrusion width", 180);
		optgroup->append_single_option_line(get_option_("extrusion_width"));
		optgroup->append_single_option_line(get_option_("first_layer_extrusion_width"));
		optgroup->append_single_option_line(get_option_("perimeter_extrusion_width"));
		optgroup->append_single_option_line(get_option_("external_perimeter_extrusion_width"));
		optgroup->append_single_option_line(get_option_("infill_extrusion_width"));
		optgroup->append_single_option_line(get_option_("solid_infill_extrusion_width"));
		optgroup->append_single_option_line(get_option_("top_infill_extrusion_width"));
		optgroup->append_single_option_line(get_option_("support_material_extrusion_width"));

		optgroup = page->new_optgroup("Overlap");
		optgroup->append_single_option_line(get_option_("infill_overlap"));

		optgroup = page->new_optgroup("Flow");
 		optgroup->append_single_option_line(get_option_("bridge_flow_ratio"));

		optgroup = page->new_optgroup("Other");
 		optgroup->append_single_option_line(get_option_("clip_multipart_objects"));
		optgroup->append_single_option_line(get_option_("elefant_foot_compensation"));
		optgroup->append_single_option_line(get_option_("xy_size_compensation"));
//		#            optgroup->append_single_option_line(get_option_("threads"));
		optgroup->append_single_option_line(get_option_("resolution"));

	page = add_options_page("Output options", "page_white_go.png");
 		optgroup = page->new_optgroup("Sequential printing");
 		optgroup->append_single_option_line(get_option_("complete_objects"));
		line = Line{ "Extruder clearance (mm)", "" };
		Option option = get_option_("extruder_clearance_radius");
		option.opt.width = 60;
		line.append_option(option);
		option = get_option_("extruder_clearance_height");
		option.opt.width = 60;
		line.append_option(option);
		optgroup->append_line(line);

		optgroup = page->new_optgroup("Output file");
 		optgroup->append_single_option_line(get_option_("gcode_comments"));
		option = get_option_("output_filename_format");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Post-processing scripts", 0);	
		option = get_option_("post_process");
		option.opt.full_width = true;
		option.opt.height = 50;
		optgroup->append_single_option_line(option);

	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes", 0);						
		option = get_option_("notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page("Dependencies", "wrench.png");
 		optgroup = page->new_optgroup("Profile dependencies");
		line = Line{ "Compatible printers", "" };
		line.widget = compatible_printers_widget_; 
		optgroup->append_line(line);
}

void CTabFilament::build()
{
	config_ = preset_bundle_->filaments.get_edited_preset().config;	
	config_def_ = config_.def();

 	auto page = add_options_page("Filament", "spool.png");
		auto optgroup = page->new_optgroup("Filament");
		optgroup->append_single_option_line(get_option_("filament_colour"));
		optgroup->append_single_option_line(get_option_("filament_diameter"));
		optgroup->append_single_option_line(get_option_("extrusion_multiplier"));
		optgroup->append_single_option_line(get_option_("filament_density"));
		optgroup->append_single_option_line(get_option_("filament_cost"));

		optgroup = page->new_optgroup("Temperature (°C)");
 		Line line = { "Extruder", "" };
		line.append_option(get_option_("first_layer_temperature"));
		line.append_option(get_option_("temperature"));
		optgroup->append_line(line);

		line = { "Bed", "" };
		line.append_option(get_option_("first_layer_bed_temperature"));
		line.append_option(get_option_("bed_temperature"));
		optgroup->append_line(line);

	page = add_options_page("Cooling", "hourglass.png");
		optgroup = page->new_optgroup("Enable");
		optgroup->append_single_option_line(get_option_("fan_always_on"));
		optgroup->append_single_option_line(get_option_("cooling"));

		line = { "", "" }; 
		line.full_width = 1;
		line.widget = cooling_description_line_widget_;
		optgroup->append_line(line);

		optgroup = page->new_optgroup("Fan settings");
		line = {"Fan speed",""};
		line.append_option(get_option_("min_fan_speed"));
		line.append_option(get_option_("max_fan_speed"));
		optgroup->append_line(line);

		optgroup->append_single_option_line(get_option_("bridge_fan_speed"));
		optgroup->append_single_option_line(get_option_("disable_fan_first_layers"));

		optgroup = page->new_optgroup("Cooling thresholds", 250);
		optgroup->append_single_option_line(get_option_("fan_below_layer_time"));
		optgroup->append_single_option_line(get_option_("slowdown_below_layer_time"));
		optgroup->append_single_option_line(get_option_("min_print_speed"));

	page = add_options_page("Advanced", "wrench.png");
		optgroup = page->new_optgroup("Filament properties");
		optgroup->append_single_option_line(get_option_("filament_type"));
		optgroup->append_single_option_line(get_option_("filament_soluble"));

 		optgroup = page->new_optgroup("Print speed override");
		optgroup->append_single_option_line(get_option_("filament_max_volumetric_speed"));

		line = {"",""};
		line.full_width = 1;
		line.widget = volumetric_speed_description_line_widget_;
		optgroup->append_line(line);

	page = add_options_page("Custom G-code", "cog.png");
		optgroup = page->new_optgroup("Start G-code", 0);
		Option option = get_option_("start_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("End G-code", 0);
		option = get_option_("end_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes", 0);
		optgroup->label_width = 0;
		option = get_option_("filament_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
 		optgroup->append_single_option_line(option);

	page = add_options_page("Dependencies", "wrench.png");
		optgroup = page->new_optgroup("Profile dependencies");
		line = {"Compatible printers", ""};
		line.widget =  compatible_printers_widget_;
		optgroup->append_line(line);
}

wxStaticText*	CTabFilament::cooling_description_line_;
wxStaticText*	CTabFilament::volumetric_speed_description_line_;
wxSizer* CTabFilament::description_line_widget_(wxWindow* parent, wxStaticText* StaticText)
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

//#include "../../libslic3r/GCodeSender.hpp";
void CTabPrinter::build()
{
	config_ = preset_bundle_->printers.get_edited_preset().config;
	config_def_ = config_.def();		// It will be used in get_option_(const std::string title)

// 	$self->{extruders_count} = scalar @{$self->{config}->nozzle_diameter};
	auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(config_.option("nozzle_diameter"));
//	size_t  extruders_count = nozzle_diameter->values.size();

	auto page = add_options_page("General", "printer_empty.png");
		auto optgroup = page->new_optgroup("Size and coordinates");

		Line line = { "Bed shape", "" };
		line.widget = bed_shape_widget_;
		optgroup->append_line(line);
		optgroup->append_single_option_line(get_option_("z_offset"));

		optgroup = page->new_optgroup("Capabilities");
		ConfigOptionDef def;
			def.type =  coInt,
			def.default_value = new ConfigOptionInt(1); 
			def.label = "Extruders";
			def.tooltip = "Number of extruders of the printer.";
			def.min = 1;
		Option option(def, "extruders_count");
		optgroup->append_single_option_line(option);
		optgroup->append_single_option_line(get_option_("single_extruder_multi_material"));

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

// 		if (!$params{ no_controller })
// 		{
		optgroup = page->new_optgroup("USB/Serial connection");
			line = {"Serial port", ""};
			Option serial_port = get_option_("serial_port");
			serial_port.side_widget = ([](wxWindow* parent){
				auto btn = new wxBitmapButton(parent, wxID_ANY, wxBitmap(wxT("var\\arrow_rotate_clockwise.png"), wxBITMAP_TYPE_PNG),
					wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
				/*if (btn->can('SetToolTipString')*/btn->SetToolTip("Rescan serial ports");
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent e) {/*_update_serial_ports*/; });
				return sizer;
			});
			auto serial_test = ([/*serial_test_btn*/](wxWindow* parent){
				auto btn = /*serial_test_btn =*/ new wxButton(parent, wxID_ANY,
					"Test", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
//				btn->SetFont($Slic3r::GUI::small_font);
				btn->SetBitmap(wxBitmap(wxT("var\\wrench.png"), wxBITMAP_TYPE_PNG));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

//				btn->Bind(wxEVT_BUTTON, []{
// 					auto sender = new GCodeSender();
// 					auto res = true;// sender->connect(
// // 						s_cache_HostConfig.serial_port,
// // 						config_->serial_speed
// //						);
// 					if (res && sender->wait_connected()) {
// 						show_info(parent, "Connection to printer works correctly.", "Success!");
// 					}
// 					else {
// 						show_error(parent, "Connection failed.");
// 					}
// 				});
				return sizer;
			});

			line.append_option(serial_port);
			line.append_option(get_option_("serial_speed"));
			line.append_widget(serial_test);
			optgroup->append_line(line);
//		}
		optgroup = page->new_optgroup("OctoPrint upload");
		// # append two buttons to the Host line
		auto octoprint_host_browse = ([] (wxWindow* parent) {
			auto btn = new wxButton(parent, wxID_ANY, "Browse…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
//			btn->SetFont($Slic3r::GUI::small_font);
			btn->SetBitmap(wxBitmap(wxT("var\\zoom.png"), wxBITMAP_TYPE_PNG));
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

// 			if (!eval "use Net::Bonjour; 1") {
// 				btn->Disable;
// 			}

//			btn->Bind(wxEVT_BUTTON, []{
				// # look for devices
// 				my $entries;
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
//			});

			return sizer;
		});
		auto octoprint_host_test = ([/*serial_test_btn*/](wxWindow* parent) {
			auto btn = /*serial_test_btn =*/ new wxButton(parent, wxID_ANY,
				"Test", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
//			btn->SetFont($Slic3r::GUI::small_font);
			btn->SetBitmap(wxBitmap(wxT("var\\wrench.png"), wxBITMAP_TYPE_PNG));
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, [](wxCommandEvent e) {
// 				my $ua = LWP::UserAgent->new;
// 				$ua->timeout(10);
// 
// 				my $res = $ua->get(
// 					"http://".$self->{config}->octoprint_host . "/api/version",
// 					'X-Api-Key' = > $self->{config}->octoprint_apikey,
// 					);
// 				if ($res->is_success) {
// 					Slic3r::GUI::show_info($self, "Connection to OctoPrint works correctly.", "Success!");
// 				}
// 				else {
// 					Slic3r::GUI::show_error($self,
// 						"I wasn't able to connect to OctoPrint (".$res->status_line . "). "
// 						. "Check hostname and OctoPrint version (at least 1.1.0 is required).");
// 				}
 			});
			return sizer;
		});

		Line host_line = optgroup->create_single_option_line(get_option_("octoprint_host"));
		host_line.append_widget(octoprint_host_browse);
		host_line.append_widget(octoprint_host_test);
		optgroup->append_line(host_line);
		optgroup->append_single_option_line(get_option_("octoprint_apikey"));

		optgroup = page->new_optgroup("Firmware");
		optgroup->append_single_option_line(get_option_("gcode_flavor"));

		optgroup = page->new_optgroup("Advanced");
		optgroup->append_single_option_line(get_option_("use_relative_e_distances"));
		optgroup->append_single_option_line(get_option_("use_firmware_retraction"));
		optgroup->append_single_option_line(get_option_("use_volumetric_e"));
		optgroup->append_single_option_line(get_option_("variable_layer_height"));

	page = add_options_page("Custom G-code", "cog.png");
		optgroup = page->new_optgroup("Start G-code", 0);
		option = get_option_("start_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("End G-code", 0);
		option = get_option_("end_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Before layer change G-code", 0);
		option = get_option_("before_layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("After layer change G-code", 0);
		option = get_option_("layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Tool change G-code", 0);
		option = get_option_("toolchange_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Between objects G-code (for sequential printing)", 0);
		option = get_option_("between_objects_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);
	
	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes", 0);
		option = get_option_("printer_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

// 	$self->{extruder_pages} = [];
	build_extruder_pages_();

// 	$self->_update_serial_ports if (!$params{ no_controller });
}

void CTabPrinter::build_extruder_pages_(){
// 	my $default_config = Slic3r::Config::Full->new;
// 
// 	foreach my $extruder_idx(@{$self->{extruder_pages}} ..$self->{extruders_count}-1) {
		//# build page
		auto page = /*$self->{extruder_pages}[$extruder_idx] =*/ add_options_page("Extruder "/* . ($extruder_idx + 1)*/, "funnel.png");
			auto optgroup = page->new_optgroup("Size");
			optgroup->append_single_option_line(get_option_("nozzle_diameter"/*, $extruder_idx*/));
		
			optgroup = page->new_optgroup("Layer height limits");
			optgroup->append_single_option_line(get_option_("min_layer_height"/*, $extruder_idx*/));
			optgroup->append_single_option_line(get_option_("max_layer_height"/*, $extruder_idx*/));
				
		
			optgroup = page->new_optgroup("Position (for multi-extruder printers)");
			optgroup->append_single_option_line(get_option_("extruder_offset"/*, $extruder_idx*/));
		
			optgroup = page->new_optgroup("Retraction");
			optgroup->append_single_option_line(get_option_("retract_length"/*, $extruder_idx*/));
			optgroup->append_single_option_line(get_option_("retract_lift"/*, $extruder_idx*/));
				Line line = { "Only lift Z", "" };
				line.append_option(get_option_("retract_lift_above"/*, $extruder_idx*/));
				line.append_option(get_option_("retract_lift_below"/*, $extruder_idx*/));
				optgroup->append_line(line);
			
			optgroup->append_single_option_line(get_option_("retract_speed"/*, $extruder_idx*/));
			optgroup->append_single_option_line(get_option_("deretract_speed"/*, $extruder_idx*/));
			optgroup->append_single_option_line(get_option_("retract_restart_extra"/*, $extruder_idx*/));
			optgroup->append_single_option_line(get_option_("retract_before_travel"/*, $extruder_idx*/));
			optgroup->append_single_option_line(get_option_("retract_layer_change"/*, $extruder_idx*/));
			optgroup->append_single_option_line(get_option_("wipe"/*, $extruder_idx*/));
			optgroup->append_single_option_line(get_option_("retract_before_wipe"/*, $extruder_idx*/));
	
			optgroup = page->new_optgroup("Retraction when tool is disabled (advanced settings for multi-extruder setups)");
			optgroup->append_single_option_line(get_option_("retract_length_toolchange"/*, $extruder_idx*/));
			optgroup->append_single_option_line(get_option_("retract_restart_extra_toolchange"/*, $extruder_idx*/));

			optgroup = page->new_optgroup("Preview");
			optgroup->append_single_option_line(get_option_("extruder_colour"/*, $extruder_idx*/));
//	}
// 
// 	// # remove extra pages
// 	if ($self->{extruders_count} <= $#{$self->{extruder_pages}}) {
// 		$_->Destroy for @{$self->{extruder_pages}}[$self->{extruders_count}..$#{$self->{extruder_pages}}];
// 		splice @{$self->{extruder_pages}}, $self->{extruders_count};
// 	}
// 
// 	// # rebuild page list
// 	my @pages_without_extruders = (grep $_->{title} !~/ ^Extruder \d + / , @{$self->{pages}});
// 	my $page_notes = pop @pages_without_extruders;
// 	@{$self->{pages}} = (
// 		@pages_without_extruders,
// 		@{$self->{extruder_pages}}[0 ..$self->{extruders_count}-1],
// 		$page_notes
// 		);
 	rebuild_page_tree();
}

//Regerenerate content of the page tree.
void CTab::rebuild_page_tree()
{
	Freeze();
	// get label of the currently selected item
	auto selected = treectrl_->GetItemText(treectrl_->GetSelection());
	auto rootItem = treectrl_->GetRootItem();
	treectrl_->DeleteChildren(rootItem);
	auto have_selection = 0;
	for (auto p : pages_)
	{
		auto itemId = treectrl_->AppendItem(rootItem, p->title(), p->iconID());
		if (p->title() == selected) {
			disable_tree_sel_changed_event_ = 1;
			treectrl_->SelectItem(itemId);
			disable_tree_sel_changed_event_ = 0;
			have_selection = 1;
		}
	}
	
	if (!have_selection) {
		// this is triggered on first load, so we don't disable the sel change event
		treectrl_->SelectItem(treectrl_->GetFirstVisibleItem());//! (treectrl->GetFirstChild(rootItem));
	}
	Thaw();
}

void CTab::OnTreeSelChange(wxTreeEvent& event)
{
	if (disable_tree_sel_changed_event_) return;
	CPage* page = nullptr;
	auto selection = treectrl_->GetItemText(treectrl_->GetSelection());
	for (auto p : pages_)
		if (p->title() == selection)
		{
			page = p.get();
			break;
		}
	if (page == nullptr) return;

	for (auto& el : pages_)
		el.get()->Hide();
	page->Show();
	hsizer_->Layout();
	Refresh();
}

void CTab::OnKeyDown(wxKeyEvent& event)
{
	event.GetKeyCode() == WXK_TAB ?
		treectrl_->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward) :
		event.Skip();
};

void CTab::save_preset(wxCommandEvent &event){};
void CTab::delete_preset(wxCommandEvent &event){};
void CTab::_toggle_show_hide_incompatible(wxCommandEvent &event){};

//	# Return a callback to create a Tab widget to mark the preferences as compatible / incompatible to the current printer.
wxSizer* CTab::compatible_printers_widget_(wxWindow* parent)
{
	auto checkbox /*= compatible_printers_checkbox*/ = new wxCheckBox(parent, wxID_ANY, "All");

	auto btn /*= compatible_printers_btn*/ = new wxButton(parent, wxID_ANY, "Set…", wxDefaultPosition, wxDefaultSize,
	wxBU_LEFT | wxBU_EXACTFIT);
//	btn->SetFont(GUI::small_font);
	btn->SetBitmap(wxBitmap(wxT("var\\printer_empty.png"), wxBITMAP_TYPE_PNG));

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

wxSizer* CTab::bed_shape_widget_(wxWindow* parent)
{
		auto btn = new wxButton(parent, wxID_ANY, "Set…", wxDefaultPosition, wxDefaultSize,
			wxBU_LEFT | wxBU_EXACTFIT);
//		btn->SetFont(Slic3r::GUI::small_font);
		btn->SetBitmap(wxBitmap(wxT("var\\printer_empty.png"), wxBITMAP_TYPE_PNG));

		auto sizer = new wxBoxSizer(wxHORIZONTAL);
		sizer->Add(btn);

		btn->Bind(wxEVT_BUTTON, ([=](wxCommandEvent e)
		{
// 			auto dlg = new BedShapeDialog->new($self, $self->{config}->bed_shape);
// 			if (dlg->ShowModal == wxID_OK)
				;// load_key_value_("bed_shape", dlg->GetValue);
		}));

 		return sizer;
}

// package Slic3r::GUI::Tab::Page;
ConfigOptionsGroupShp CPage::new_optgroup(std::string title, int noncommon_label_width /*= -1*/)
{
	//! config_ have to be "right"
	ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(this, title, config_);
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
	optgroups.push_back(optgroup);

	return optgroup;
}

} // GUI
} // Slic3r
