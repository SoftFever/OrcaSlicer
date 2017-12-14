#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/menu.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>

#include "Tab.h"
#include "PresetBundle.hpp"

namespace Slic3r {
namespace GUI {

// sub new
void CTab::create_preset_tab()
{
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

	//!-----------------------EXP
	// Vertical sizer to hold selected page
// 	auto *scrolled_win = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
// 	wxBoxSizer *vs = new wxBoxSizer(wxVERTICAL);
// 	scrolled_win->SetSizer(vs);
// 	scrolled_win->SetScrollbars(1, 1, 1, 1);
// 	hsizer_->Add(scrolled_win, 1, wxEXPAND | wxLEFT, 5);
// 
// 	wxSizer* sbs = new wxStaticBoxSizer(new wxStaticBox(scrolled_win, wxID_ANY, "Trulala"), wxVERTICAL);
// 	vs->Add(sbs, 0, wxEXPAND | wxALL, 10);
// 	sbs = new wxBoxSizer(wxVERTICAL);
// 	vs->Add(sbs, 0, wxEXPAND | wxALL, 10);
// 	sbs = new wxStaticBoxSizer(new wxStaticBox(scrolled_win, wxID_ANY, "LuTrulala"), wxVERTICAL);
// 	vs->Add(sbs, 0, wxEXPAND | wxALL, 10);


// 	auto *page_sizer = new wxBoxSizer(wxVERTICAL);
// 	hsizer_->Add(page_sizer, 1, wxEXPAND | wxLEFT, 5);

// 	wxStaticBox* box = new wxStaticBox(panel, wxID_ANY, "Filament");
// 	page_sizer->Add(new wxStaticBoxSizer(box, wxHORIZONTAL), 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 10);
// 
// 	//Horizontal sizer to hold the tree and the selected page.
// 	wxStaticBoxSizer* tmp_hsizer = new wxStaticBoxSizer(wxHORIZONTAL, panel, "Experimental Box");
// 	page_sizer->Add(tmp_hsizer, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 10);
// 
// 	auto *grid_sizer = new wxFlexGridSizer(0, 4, 0, 0);
// 	grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
// 	tmp_hsizer->Add(grid_sizer, 0, wxEXPAND | wxALL, /*&Wx::wxMAC ? 0 :*/ 5);
// 
// 	wxStaticText *label = new wxStaticText(panel, wxID_ANY, "Label1", wxDefaultPosition, wxSize(200,-1));
// 	auto *textctrl = new wxTextCtrl(panel, wxID_ANY, "TruLaLa1");
// 	grid_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
// 	grid_sizer->Add(textctrl, 0, wxALIGN_CENTER_VERTICAL, 0);
// 
// 	label = new wxStaticText(panel, wxID_ANY, "Labelszdfdghhjk2");
// 	textctrl = new wxTextCtrl(panel, wxID_ANY, "TruLaLa2");
// 	grid_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
// 	grid_sizer->Add(textctrl, 0, wxALIGN_CENTER_VERTICAL, 0);
// 
// 	label = new wxStaticText(panel, wxID_ANY, "Label3");
// 	textctrl = new wxTextCtrl(panel, wxID_ANY, "TruLaLa3");
// 	grid_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
// 	grid_sizer->Add(textctrl, 0, wxALIGN_CENTER_VERTICAL, 0);
// 
// 	label = new wxStaticText(panel, wxID_ANY, "Label4");
// 	textctrl = new wxTextCtrl(panel, wxID_ANY, "TruLaLa4");
// 	grid_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
// 	grid_sizer->Add(textctrl, 0, wxALIGN_CENTER_VERTICAL, 0);
// 
// 	box = new wxStaticBox(panel, wxID_ANY, "Print");
// 	page_sizer->Add(new wxStaticBoxSizer(box, wxHORIZONTAL), 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 10);
	//!------------------------

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


	return;//$self;
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

void CTabPrint::build()
{
//	$self->{presets} = wxTheApp->{preset_bundle}->print;
//	$self->{config} = $self->{presets}->get_edited_preset->config;

	PresetCollection *prints = new PresetCollection(Preset::TYPE_PRINT, Preset::print_options());
	config_ = prints->get_edited_preset().config;
	config_def = config_.def();

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
	page->set_config(&config_);

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
		option.opt.full_width = 1;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup("Post-processing scripts");	//! 			label_width = > 0,
		option = get_option_("post_process");
		option.opt.full_width = 1;
		option.opt.height = 50;
		optgroup->append_single_option_line(option);

	page = add_options_page("Notes", "note.png");
		optgroup = page->new_optgroup("Notes");						//!				label_width = > 0,
		option = get_option_("notes");
		option.opt.full_width = 1;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page("Dependencies", "wrench.png");
 		optgroup = page->new_optgroup("Profile dependencies");
		line = Line{ "Compatible printers", "" };
//		line.widget = ? ? ? ; //!		 widget = > $self->_compatible_printers_widget,
		optgroup->append_line(line);
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

// package Slic3r::GUI::Tab::Page;
ConfigOptionsGroupShp CPage::new_optgroup(std::string title, size_t label_width /*= 0*/)
{
	//! config_ have to be "right"
	ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(this, title, config_);
	if (label_width != 0)
		optgroup->label_width = label_width;

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
