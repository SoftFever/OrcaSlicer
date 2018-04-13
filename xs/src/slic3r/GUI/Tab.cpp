#include "../../libslic3r/GCodeSender.hpp"
#include "Tab.hpp"
#include "PresetBundle.hpp"
#include "PresetHints.hpp"
#include "../../libslic3r/Utils.hpp"

#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/OctoPrint.hpp"
#include "BonjourDialog.hpp"
#include "WipeTowerDialog.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>

#include <boost/algorithm/string/predicate.hpp>
#include "wxExtensions.hpp"

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
	/*
	m_cc_presets_choice = new wxComboCtrl(panel, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, wxCB_READONLY);
	wxDataViewTreeCtrlComboPopup* popup = new wxDataViewTreeCtrlComboPopup;
	if (popup != nullptr)
	{
		// FIXME If the following line is removed, the combo box popup list will not react to mouse clicks.
		//  On the other side, with this line the combo box popup cannot be closed by clicking on the combo button on Windows 10.
//		m_cc_presets_choice->UseAltPopupWindow();

//		m_cc_presets_choice->EnablePopupAnimation(false);
		m_cc_presets_choice->SetPopupControl(popup);
		popup->SetStringValue(from_u8("Text1"));

		popup->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this, popup](wxCommandEvent& evt)
		{
			auto selected = popup->GetItemText(popup->GetSelection());
			if (selected != _(L("System presets")) && selected != _(L("Default presets")))
			{
				m_cc_presets_choice->SetText(selected);
				std::string selected_string = selected.ToUTF8().data();
#ifdef __APPLE__
#else
 				select_preset(selected_string);
#endif
			}				
		});

// 		popup->Bind(wxEVT_KEY_DOWN, [popup](wxKeyEvent& evt) { popup->OnKeyEvent(evt); });
// 		popup->Bind(wxEVT_KEY_UP, [popup](wxKeyEvent& evt) { popup->OnKeyEvent(evt); });

		auto icons = new wxImageList(16, 16, true, 1);
		popup->SetImageList(icons);
		icons->Add(*new wxIcon(from_u8(Slic3r::var("flag-green-icon.png")), wxBITMAP_TYPE_PNG));
		icons->Add(*new wxIcon(from_u8(Slic3r::var("flag-red-icon.png")), wxBITMAP_TYPE_PNG));
	}
*/
	auto color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);

	//buttons
	wxBitmap bmpMenu;
	bmpMenu = wxBitmap(from_u8(Slic3r::var("disk.png")), wxBITMAP_TYPE_PNG);
	m_btn_save_preset = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	if (wxMSW) m_btn_save_preset->SetBackgroundColour(color);
	bmpMenu = wxBitmap(from_u8(Slic3r::var("delete.png")), wxBITMAP_TYPE_PNG);
	m_btn_delete_preset = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	if (wxMSW) m_btn_delete_preset->SetBackgroundColour(color);

	m_show_incompatible_presets = false;
	m_bmp_show_incompatible_presets = new wxBitmap(from_u8(Slic3r::var("flag-red-icon.png")), wxBITMAP_TYPE_PNG);
	m_bmp_hide_incompatible_presets = new wxBitmap(from_u8(Slic3r::var("flag-green-icon.png")), wxBITMAP_TYPE_PNG);
	m_btn_hide_incompatible_presets = new wxBitmapButton(panel, wxID_ANY, *m_bmp_hide_incompatible_presets, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	if (wxMSW) m_btn_hide_incompatible_presets->SetBackgroundColour(color);

	m_btn_save_preset->SetToolTip(_(L("Save current ")) + m_title);
	m_btn_delete_preset->SetToolTip(_(L("Delete this preset")));
	m_btn_delete_preset->Disable();

	m_undo_btn = new wxButton(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER);
	m_undo_to_sys_btn = new wxButton(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER);
	if (wxMSW) {
		m_undo_btn->SetBackgroundColour(color);
		m_undo_to_sys_btn->SetBackgroundColour(color);
	}
	m_undo_btn->SetBitmap(wxBitmap(from_u8(var("bullet_white.png")), wxBITMAP_TYPE_PNG));
	m_undo_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent){ on_back_to_initial_value(); }));
	m_undo_to_sys_btn->SetBitmap(wxBitmap(from_u8(var("bullet_white.png")), wxBITMAP_TYPE_PNG));
	m_undo_to_sys_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent){ on_back_to_sys_value(); }));

	m_hsizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_hsizer, 0, wxBOTTOM, 3);
	m_hsizer->Add(m_presets_choice, 1, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
	m_hsizer->AddSpacer(4);
	m_hsizer->Add(m_btn_save_preset, 0, wxALIGN_CENTER_VERTICAL);
	m_hsizer->AddSpacer(4);
	m_hsizer->Add(m_btn_delete_preset, 0, wxALIGN_CENTER_VERTICAL);
	m_hsizer->AddSpacer(16);
	m_hsizer->Add(m_btn_hide_incompatible_presets, 0, wxALIGN_CENTER_VERTICAL);
	m_hsizer->AddSpacer(64);
	m_hsizer->Add(m_undo_to_sys_btn, 0, wxALIGN_CENTER_VERTICAL);
	m_hsizer->Add(m_undo_btn, 0, wxALIGN_CENTER_VERTICAL);
// 	m_hsizer->AddSpacer(64);
// 	m_hsizer->Add(m_cc_presets_choice, 1, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);

	//Horizontal sizer to hold the tree and the selected page.
	m_hsizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_hsizer, 1, wxEXPAND, 0);


/*


	//temporary left vertical sizer
	m_left_sizer = new wxBoxSizer(wxVERTICAL);
	m_hsizer->Add(m_left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);

	// tree
	m_presetctrl = new wxDataViewTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(200, -1), wxDV_NO_HEADER);
	m_left_sizer->Add(m_presetctrl, 1, wxEXPAND);
	m_preset_icons = new wxImageList(16, 16, true, 1);
	m_presetctrl->SetImageList(m_preset_icons);
	m_preset_icons->Add(*new wxIcon(from_u8(Slic3r::var("flag-green-icon.png")), wxBITMAP_TYPE_PNG));
	m_preset_icons->Add(*new wxIcon(from_u8(Slic3r::var("flag-red-icon.png")), wxBITMAP_TYPE_PNG));

	m_presetctrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxCommandEvent& evt)
	{
		auto selected = m_presetctrl->GetItemText(m_presetctrl->GetSelection());
		if (selected != _(L("System presets")) && selected != _(L("Default presets")))
		{
			std::string selected_string = selected.ToUTF8().data();
#ifdef __APPLE__
#else
			select_preset(selected_string);
#endif
		}
	});

*/

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

void Tab::load_initial_data()
{
	m_config = &m_presets->get_edited_preset().config;
	m_nonsys_btn_icon = m_presets->get_selected_preset_parent() == nullptr ?
						"bullet_white.png" : "sys_unlock.png";
}

PageShp Tab::add_options_page(const wxString& title, const std::string& icon, bool is_extruder_pages/* = false*/)
{
	// Index of icon in an icon list $self->{icons}.
	auto icon_idx = 0;
	if (!icon.empty()) {
		icon_idx = (m_icon_index.find(icon) == m_icon_index.end()) ? -1 : m_icon_index.at(icon);
		if (icon_idx == -1) {
			// Add a new icon to the icon list.
			const auto img_icon = new wxIcon(from_u8(Slic3r::var(icon)), wxBITMAP_TYPE_PNG);
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

template<class T>
void add_correct_opts_to_dirty_options(const std::string &opt_key, std::vector<std::string> *vec, TabPrinter *tab)
{
	auto opt_init = static_cast<T*>(tab->m_presets->get_selected_preset().config.option(opt_key));
	auto opt_cur = static_cast<T*>(tab->m_config->option(opt_key));
	int opt_init_max_id = opt_init->values.size()-1;
	for (int i = 0; i < opt_cur->values.size(); i++)
	{
		int init_id = i <= opt_init_max_id ? i : 0;
		if (opt_cur->values[i] != opt_init->values[init_id])
			vec->emplace_back(opt_key + "#" + std::to_string(i));
	}
}

template<class T>
void add_correct_opts_to_sys_options(const std::string &opt_key, std::vector<std::string> *vec, TabPrinter *tab)
{
	const Preset* sys_preset = tab->m_presets->get_selected_preset_parent();
	if (sys_preset == nullptr)
		return;
	T *opt_cur = static_cast<T*>(tab->m_config->option(opt_key));
	const T *opt_sys = static_cast<const T*>(sys_preset->config.option(opt_key));
	int opt_max_id = opt_sys->values.size()-1;
	for (int i = 0; i < opt_cur->values.size(); i++)
	{
		int init_id = i <= opt_max_id ? i : 0;
		if (opt_cur->values[i] == opt_sys->values[init_id])
			vec->emplace_back(opt_key + "#" + std::to_string(i));
	}
}

// Update UI according to changes
void Tab::update_changed_ui()
{
	auto dirty_options = m_presets->current_dirty_options();

	if (name() == "printer"){
		// Update dirty_options in case changes of Extruder's options 
		TabPrinter* tab = static_cast<TabPrinter*>(this);
		m_dirty_options.resize(0);
		for (auto opt_key : dirty_options)
		{
			if (opt_key == "bed_shape"){ m_dirty_options.emplace_back(opt_key);		continue; }
			switch (m_config->option(opt_key)->type())
			{
			case coInts:	add_correct_opts_to_dirty_options<ConfigOptionInts		>(opt_key, &m_dirty_options, tab);	break;
			case coBools:	add_correct_opts_to_dirty_options<ConfigOptionBools		>(opt_key, &m_dirty_options, tab);	break;
			case coFloats:	add_correct_opts_to_dirty_options<ConfigOptionFloats	>(opt_key, &m_dirty_options, tab);	break;
			case coStrings:	add_correct_opts_to_dirty_options<ConfigOptionStrings	>(opt_key, &m_dirty_options, tab);	break;
			case coPercents:add_correct_opts_to_dirty_options<ConfigOptionPercents	>(opt_key, &m_dirty_options, tab);	break;
			case coPoints:	add_correct_opts_to_dirty_options<ConfigOptionPoints	>(opt_key, &m_dirty_options, tab);	break;
			default:		m_dirty_options.emplace_back(opt_key);		break;
			}
		}
		if (tab->m_initial_extruders_count != tab->m_extruders_count)
			m_dirty_options.emplace_back("extruders_count");

		m_sys_options.resize(0);
		const auto sys_preset = m_presets->get_selected_preset_parent();
		if (sys_preset){
			for (auto opt_key : m_config->keys())
			{
				if (opt_key == "bed_shape"){ 
					if (*tab->m_config->option(opt_key) == *sys_preset->config.option(opt_key))
						m_sys_options.emplace_back(opt_key);		
					continue; 
				}
				switch (m_config->option(opt_key)->type())
				{
				case coInts:	add_correct_opts_to_sys_options<ConfigOptionInts	>(opt_key, &m_sys_options, tab);	break;
				case coBools:	add_correct_opts_to_sys_options<ConfigOptionBools	>(opt_key, &m_sys_options, tab);	break;
				case coFloats:	add_correct_opts_to_sys_options<ConfigOptionFloats	>(opt_key, &m_sys_options, tab);	break;
				case coStrings:	add_correct_opts_to_sys_options<ConfigOptionStrings	>(opt_key, &m_sys_options, tab);	break;
				case coPercents:add_correct_opts_to_sys_options<ConfigOptionPercents>(opt_key, &m_sys_options, tab);	break;
				case coPoints:	add_correct_opts_to_sys_options<ConfigOptionPoints	>(opt_key, &m_sys_options, tab);	break;
				default:{
					const ConfigOption *opt_cur = tab->m_config->option(opt_key);
					const ConfigOption *opt_sys = sys_preset->config.option(opt_key);
					if (opt_cur != nullptr && opt_sys != nullptr && *opt_cur == *opt_sys)
						m_sys_options.emplace_back(opt_key);		
					break;
				}
				}
			}

			if (tab->m_sys_extruders_count == tab->m_extruders_count)
				m_sys_options.emplace_back("extruders_count");
		}
	}
	else{
		m_sys_options = m_presets->system_equal_options();
		m_dirty_options = dirty_options;
	}

	Freeze();
	//update options "decoration"
	for (const auto opt_key : m_full_options_list)
	{
		bool is_nonsys_value = false;
		bool is_modified_value = true;
		std::string sys_icon = "sys_lock.png";
		std::string icon = "action_undo.png";
		wxColour color = get_sys_label_clr();
		if (find(m_sys_options.begin(), m_sys_options.end(), opt_key) == m_sys_options.end()) {
			is_nonsys_value = true;
			sys_icon = m_nonsys_btn_icon;
			if(find(m_dirty_options.begin(), m_dirty_options.end(), opt_key) == m_dirty_options.end())
				color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
			else
				color = get_modified_label_clr();
		}
		if (find(m_dirty_options.begin(), m_dirty_options.end(), opt_key) == m_dirty_options.end())
		{
			is_modified_value = false;
			icon = "bullet_white.png";
		}
		if (opt_key == "bed_shape" || opt_key == "compatible_printers") {
			if (m_colored_Label != nullptr)	{
				m_colored_Label->SetForegroundColour(color);
				m_colored_Label->Refresh(true);
			}
			continue;
		}

		Field* field = get_field(opt_key);
		if (field == nullptr) continue;
		field->m_is_nonsys_value = is_nonsys_value;
		field->m_is_modified_value = is_modified_value;
		field->m_Undo_btn->SetBitmap(wxBitmap(from_u8(var(icon)), wxBITMAP_TYPE_PNG));
		field->m_Undo_to_sys_btn->SetBitmap(wxBitmap(from_u8(var(sys_icon)), wxBITMAP_TYPE_PNG));
		if (field->m_Label != nullptr){
			field->m_Label->SetForegroundColour(color);
			field->m_Label->Refresh(true);
		}
	}
	Thaw();

	wxTheApp->CallAfter([this]() {
		update_changed_tree_ui();
	});
}

template<class T>
void add_correct_opts_to_full_options_list(const std::string &opt_key, std::vector<std::string> *vec, TabPrinter *tab)
{
	T *opt_cur = static_cast<T*>(tab->m_config->option(opt_key));
	for (int i = 0; i < opt_cur->values.size(); i++)
		vec->emplace_back(opt_key + "#" + std::to_string(i));
}

void Tab::update_full_options_list()
{
	if (!m_full_options_list.empty())
		m_full_options_list.resize(0);

	if (m_name != "printer"){
		m_full_options_list = m_config->keys();
		return;
	}

	TabPrinter* tab = static_cast<TabPrinter*>(this);
	for (const auto opt_key : m_config->keys())
	{
		if (opt_key == "bed_shape"){
			m_full_options_list.emplace_back(opt_key);
			continue;
		}
		switch (m_config->option(opt_key)->type())
		{
		case coInts:	add_correct_opts_to_full_options_list<ConfigOptionInts		>(opt_key, &m_full_options_list, tab);	break;
		case coBools:	add_correct_opts_to_full_options_list<ConfigOptionBools		>(opt_key, &m_full_options_list, tab);	break;
		case coFloats:	add_correct_opts_to_full_options_list<ConfigOptionFloats	>(opt_key, &m_full_options_list, tab);	break;
		case coStrings:	add_correct_opts_to_full_options_list<ConfigOptionStrings	>(opt_key, &m_full_options_list, tab);	break;
		case coPercents:add_correct_opts_to_full_options_list<ConfigOptionPercents	>(opt_key, &m_full_options_list, tab);	break;
		case coPoints:	add_correct_opts_to_full_options_list<ConfigOptionPoints	>(opt_key, &m_full_options_list, tab);	break;
		default:		m_full_options_list.emplace_back(opt_key);		break;
		}
	}
	m_full_options_list.emplace_back("extruders_count");
}

void Tab::update_sys_ui_after_sel_preset()
{
	for (const auto opt_key : m_full_options_list){
		Field* field = get_field(opt_key);
		if (field != nullptr){
			field->m_Undo_to_sys_btn->SetBitmap(wxBitmap(from_u8(var(m_nonsys_btn_icon)), wxBITMAP_TYPE_PNG));
			field->m_is_nonsys_value = true;
			if (field->m_Label != nullptr){
				field->m_Label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
				field->m_Label->Refresh(true);
			}
		}
	}
	m_sys_options.resize(0);
}

void Tab::get_sys_and_mod_flags(const std::string& opt_key, bool& sys_page, bool& modified_page)
{
	if (sys_page && find(m_sys_options.begin(), m_sys_options.end(), opt_key) == m_sys_options.end())
		sys_page = false;
	if (!modified_page && find(m_dirty_options.begin(), m_dirty_options.end(), opt_key) != m_dirty_options.end())
		modified_page = true;
}

void Tab::update_changed_tree_ui()
{
	auto cur_item = m_treectrl->GetFirstVisibleItem();
	auto selection = m_treectrl->GetItemText(m_treectrl->GetSelection());
	while (cur_item){
		auto title = m_treectrl->GetItemText(cur_item);
		for (auto page : m_pages)
		{
			if (page->title() != title)
				continue;
			bool sys_page = true;
			bool modified_page = false;
			if (title == _("General")){
				std::initializer_list<const char*> optional_keys{ "extruders_count", "bed_shape" };
				for (auto &opt_key : optional_keys) {
					get_sys_and_mod_flags(opt_key, sys_page, modified_page);
				}
			}
			if (title == _("Dependencies")){
				get_sys_and_mod_flags("compatible_printers", sys_page, modified_page);
			}
			for (auto group : page->m_optgroups)
			{
				if (!sys_page && modified_page)
					break;
				for (t_opt_map::iterator it = group->m_opt_map.begin(); it != group->m_opt_map.end(); ++it) {
					const std::string& opt_key = it->first;
					get_sys_and_mod_flags(opt_key, sys_page, modified_page);
				}
			}
			if (sys_page)
				m_treectrl->SetItemTextColour(cur_item, get_sys_label_clr());
			else if (modified_page)
				m_treectrl->SetItemTextColour(cur_item, get_modified_label_clr());
			else
				m_treectrl->SetItemTextColour(cur_item, wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));

			page->m_is_nonsys_values = !sys_page;
			page->m_is_modified_values = modified_page;

			if (selection == title){
				m_is_nonsys_values = page->m_is_nonsys_values;
				m_is_modified_values = page->m_is_modified_values;
			}
			break;
		}
		auto next_item = m_treectrl->GetNextVisible(cur_item);
		cur_item = next_item;
	}
	update_undo_buttons();
}

void Tab::update_undo_buttons()
{
	const std::string& undo_icon = !m_is_modified_values ? "bullet_white.png" : "action_undo.png";
	const std::string& undo_to_sys_icon = m_is_nonsys_values ? m_nonsys_btn_icon : "sys_lock.png";

	m_undo_btn->SetBitmap(wxBitmap(from_u8(var(undo_icon)), wxBITMAP_TYPE_PNG));
	m_undo_to_sys_btn->SetBitmap(wxBitmap(from_u8(var(undo_to_sys_icon)), wxBITMAP_TYPE_PNG));
}

void Tab::on_back_to_initial_value()
{
	if (!m_is_modified_values) return;

	auto selection = m_treectrl->GetItemText(m_treectrl->GetSelection());
	for (auto page : m_pages)
		if (page->title() == selection)	{
			for (auto group : page->m_optgroups){
				if (group->title == _("Capabilities")){
					if (find(m_dirty_options.begin(), m_dirty_options.end(), "extruders_count") != m_dirty_options.end())
						group->back_to_initial_value("extruders_count");
				}
				if (group->title == _("Size and coordinates")){
					if (find(m_dirty_options.begin(), m_dirty_options.end(), "bed_shape") != m_dirty_options.end())
						group->back_to_initial_value("bed_shape");
				}
				if (group->title == _("Profile dependencies")){
					if (find(m_dirty_options.begin(), m_dirty_options.end(), "compatible_printers") != m_dirty_options.end())
						group->back_to_initial_value("compatible_printers");

					bool is_empty = m_config->option<ConfigOptionStrings>("compatible_printers")->values.empty();
					m_compatible_printers_checkbox->SetValue(is_empty);
					is_empty ? m_compatible_printers_btn->Disable() : m_compatible_printers_btn->Enable();
				}
				for (t_opt_map::iterator it = group->m_opt_map.begin(); it != group->m_opt_map.end(); ++it) {
					const std::string& opt_key = it->first;
					if (find(m_dirty_options.begin(), m_dirty_options.end(), opt_key) != m_dirty_options.end())
						group->back_to_initial_value(opt_key);
				}
			}
			break;
		}
	update_changed_ui();
}

void Tab::on_back_to_sys_value()
{
	if (!m_is_nonsys_values) return;

	auto selection = m_treectrl->GetItemText(m_treectrl->GetSelection());
	for (auto page : m_pages)
		if (page->title() == selection)	{
			for (auto group : page->m_optgroups) {
				if (group->title == _("Capabilities")){
					if (find(m_sys_options.begin(), m_sys_options.end(), "extruders_count") == m_sys_options.end())
						group->back_to_sys_value("extruders_count");
				}
				if (group->title == _("Size and coordinates")){
					if (find(m_sys_options.begin(), m_sys_options.end(), "bed_shape") == m_sys_options.end())
						group->back_to_sys_value("bed_shape");
				}
				if (group->title == _("Profile dependencies")){
					if (find(m_sys_options.begin(), m_sys_options.end(), "compatible_printers") == m_sys_options.end())
						group->back_to_sys_value("compatible_printers");

					bool is_empty = m_config->option<ConfigOptionStrings>("compatible_printers")->values.empty();
					m_compatible_printers_checkbox->SetValue(is_empty);
					is_empty ? m_compatible_printers_btn->Disable() : m_compatible_printers_btn->Enable();
				}
				for (t_opt_map::iterator it = group->m_opt_map.begin(); it != group->m_opt_map.end(); ++it) {
					const std::string& opt_key = it->first;
					if (find(m_sys_options.begin(), m_sys_options.end(), opt_key) == m_sys_options.end())
						group->back_to_sys_value(opt_key);
				}
			}
			break;
		}
	update_changed_ui();
}

// Update the combo box label of the selected preset based on its "dirty" state,
// comparing the selected preset config with $self->{config}.
void Tab::update_dirty(){
	m_presets->update_dirty_ui(m_presets_choice);
	on_presets_changed();	
	update_changed_ui();
//	update_dirty_presets(m_cc_presets_choice);
}

void Tab::update_tab_ui()
{
	m_presets->update_tab_ui(m_presets_choice, m_show_incompatible_presets);
// 	update_tab_presets(m_cc_presets_choice, m_show_incompatible_presets);
// 	update_presetsctrl(m_presetctrl, m_show_incompatible_presets);
}

// Load a provied DynamicConfig into the tab, modifying the active preset.
// This could be used for example by setting a Wipe Tower position by interactive manipulation in the 3D view.
void Tab::load_config(const DynamicPrintConfig& config)
{
	bool modified = 0;
	for(auto opt_key : m_config->diff(config)) {
		m_config->set_key_value(opt_key, config.option(opt_key)->clone());
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

Field* Tab::get_field(const t_config_option_key& opt_key, int opt_index/* = -1*/) const
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
bool Tab::set_value(const t_config_option_key& opt_key, const boost::any& value){
	bool changed = false;
	for(auto page: m_pages) {
		if (page->set_value(opt_key, value))
		changed = true;
	}
	return changed;
}

// To be called by custom widgets, load a value into a config,
// update the preset selection boxes (the dirty flags)
void Tab::load_key_value(const std::string& opt_key, const boost::any& value)
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

void Tab::on_value_change(const std::string& opt_key, const boost::any& value)
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
	if (opt_key == "fill_density")
	{
		boost::any val = get_optgroup()->get_config_value(*m_config, opt_key);
		get_optgroup()->set_value(opt_key, val);
	}
	if (opt_key == "support_material" || opt_key == "support_material_buildplate_only")
	{
		wxString new_selection = !m_config->opt_bool("support_material") ?
								_("None") :
								m_config->opt_bool("support_material_buildplate_only") ?
									_("Support on build plate only") :
									_("Everywhere");
		get_optgroup()->set_value("support", new_selection);
	}
	if (opt_key == "brim_width")
	{
		bool val = m_config->opt_float("brim_width") > 0.0 ? true : false;
		get_optgroup()->set_value("brim", val);
	}

    if (opt_key == "wipe_tower" || opt_key == "single_extruder_multi_material" || opt_key == "extruders_count" )
        update_wiping_button_visibility();

	update();
}


// Show/hide the 'purging volumes' button
void Tab::update_wiping_button_visibility() {
    bool wipe_tower_enabled = dynamic_cast<ConfigOptionBool*>(  (m_preset_bundle->prints.get_edited_preset().config  ).option("wipe_tower"))->value;
    bool multiple_extruders = dynamic_cast<ConfigOptionFloats*>((m_preset_bundle->printers.get_edited_preset().config).option("nozzle_diameter"))->values.size() > 1;
    bool single_extruder_mm = dynamic_cast<ConfigOptionBool*>(  (m_preset_bundle->printers.get_edited_preset().config).option("single_extruder_multi_material"))->value;

    if (wipe_tower_enabled && multiple_extruders && single_extruder_mm)
        get_wiping_dialog_button()->Show();
    else get_wiping_dialog_button()->Hide();

    (get_wiping_dialog_button()->GetParent())->Layout();
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

	const Preset* parent = m_presets->get_selected_preset_parent();
	const wxString description_line = parent == nullptr ?
		_(L("It's default preset")) : parent == &m_presets->get_selected_preset() ?
		_(L("It's system preset")) :
		_(L("Current preset is inherited from")) + ":\n" + parent->name;
	m_parent_preset_description_line->SetText(description_line);
}

void Tab::update_frequently_changed_parameters()
{
	boost::any value = get_optgroup()->get_config_value(*m_config, "fill_density");
	get_optgroup()->set_value("fill_density", value);

	wxString new_selection = !m_config->opt_bool("support_material") ?
							_("None") :
							m_config->opt_bool("support_material_buildplate_only") ?
								_("Support on build plate only") :
								_("Everywhere");
	get_optgroup()->set_value("support", new_selection);

	bool val = m_config->opt_float("brim_width") > 0.0 ? true : false;
	get_optgroup()->set_value("brim", val);

	update_wiping_button_visibility();
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
	load_initial_data();

	auto page = add_options_page(_(L("Layers and perimeters")), "layers.png");
		auto optgroup = page->new_optgroup(_(L("Layer height")));
		optgroup->append_single_option_line("layer_height");
		optgroup->append_single_option_line("first_layer_height");

		optgroup = page->new_optgroup(_(L("Vertical shells")));
		optgroup->append_single_option_line("perimeters");
		optgroup->append_single_option_line("spiral_vase");

		Line line { "", "" };
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_recommended_thin_wall_thickness_description_line);
		};
		optgroup->append_line(line);

		optgroup = page->new_optgroup(_(L("Horizontal shells")));
		line = { _(L("Solid layers")), "" };
		line.append_option(optgroup->get_option("top_solid_layers"));
		line.append_option(optgroup->get_option("bottom_solid_layers"));
		optgroup->append_line(line);

		optgroup = page->new_optgroup(_(L("Quality (slower slicing)")));
		optgroup->append_single_option_line("extra_perimeters");
		optgroup->append_single_option_line("ensure_vertical_shell_thickness");
		optgroup->append_single_option_line("avoid_crossing_perimeters");
		optgroup->append_single_option_line("thin_walls");
		optgroup->append_single_option_line("overhangs");

		optgroup = page->new_optgroup(_(L("Advanced")));
		optgroup->append_single_option_line("seam_position");
		optgroup->append_single_option_line("external_perimeters_first");

	page = add_options_page(_(L("Infill")), "infill.png");
		optgroup = page->new_optgroup(_(L("Infill")));
		optgroup->append_single_option_line("fill_density");
		optgroup->append_single_option_line("fill_pattern");
		optgroup->append_single_option_line("external_fill_pattern");

		optgroup = page->new_optgroup(_(L("Reducing printing time")));
		optgroup->append_single_option_line("infill_every_layers");
		optgroup->append_single_option_line("infill_only_where_needed");

		optgroup = page->new_optgroup(_(L("Advanced")));
		optgroup->append_single_option_line("solid_infill_every_layers");
		optgroup->append_single_option_line("fill_angle");
		optgroup->append_single_option_line("solid_infill_below_area");
		optgroup->append_single_option_line("bridge_angle");
		optgroup->append_single_option_line("only_retract_when_crossing_perimeters");
		optgroup->append_single_option_line("infill_first");

	page = add_options_page(_(L("Skirt and brim")), "box.png");
		optgroup = page->new_optgroup(_(L("Skirt")));
		optgroup->append_single_option_line("skirts");
		optgroup->append_single_option_line("skirt_distance");
		optgroup->append_single_option_line("skirt_height");
		optgroup->append_single_option_line("min_skirt_length");

		optgroup = page->new_optgroup(_(L("Brim")));
		optgroup->append_single_option_line("brim_width");

	page = add_options_page(_(L("Support material")), "building.png");
		optgroup = page->new_optgroup(_(L("Support material")));
		optgroup->append_single_option_line("support_material");
		optgroup->append_single_option_line("support_material_threshold");
		optgroup->append_single_option_line("support_material_enforce_layers");

		optgroup = page->new_optgroup(_(L("Raft")));
		optgroup->append_single_option_line("raft_layers");
//		# optgroup->append_single_option_line(get_option_("raft_contact_distance");

		optgroup = page->new_optgroup(_(L("Options for support material and raft")));
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

	page = add_options_page(_(L("Speed")), "time.png");
		optgroup = page->new_optgroup(_(L("Speed for print moves")));
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

		optgroup = page->new_optgroup(_(L("Speed for non-print moves")));
		optgroup->append_single_option_line("travel_speed");

		optgroup = page->new_optgroup(_(L("Modifiers")));
		optgroup->append_single_option_line("first_layer_speed");

		optgroup = page->new_optgroup(_(L("Acceleration control (advanced)")));
		optgroup->append_single_option_line("perimeter_acceleration");
		optgroup->append_single_option_line("infill_acceleration");
		optgroup->append_single_option_line("bridge_acceleration");
		optgroup->append_single_option_line("first_layer_acceleration");
		optgroup->append_single_option_line("default_acceleration");

		optgroup = page->new_optgroup(_(L("Autospeed (advanced)")));
		optgroup->append_single_option_line("max_print_speed");
		optgroup->append_single_option_line("max_volumetric_speed");
		optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_positive");
		optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_negative");

	page = add_options_page(_(L("Multiple Extruders")), "funnel.png");
		optgroup = page->new_optgroup(_(L("Extruders")));
		optgroup->append_single_option_line("perimeter_extruder");
		optgroup->append_single_option_line("infill_extruder");
		optgroup->append_single_option_line("solid_infill_extruder");
		optgroup->append_single_option_line("support_material_extruder");
		optgroup->append_single_option_line("support_material_interface_extruder");

		optgroup = page->new_optgroup(_(L("Ooze prevention")));
		optgroup->append_single_option_line("ooze_prevention");
		optgroup->append_single_option_line("standby_temperature_delta");

		optgroup = page->new_optgroup(_(L("Wipe tower")));
		optgroup->append_single_option_line("wipe_tower");
		optgroup->append_single_option_line("wipe_tower_x");
		optgroup->append_single_option_line("wipe_tower_y");
		optgroup->append_single_option_line("wipe_tower_width");
		optgroup->append_single_option_line("wipe_tower_rotation_angle");
        optgroup->append_single_option_line("wipe_tower_bridging");

		optgroup = page->new_optgroup(_(L("Advanced")));
		optgroup->append_single_option_line("interface_shells");

	page = add_options_page(_(L("Advanced")), "wrench.png");
		optgroup = page->new_optgroup(_(L("Extrusion width")));
		optgroup->append_single_option_line("extrusion_width");
		optgroup->append_single_option_line("first_layer_extrusion_width");
		optgroup->append_single_option_line("perimeter_extrusion_width");
		optgroup->append_single_option_line("external_perimeter_extrusion_width");
		optgroup->append_single_option_line("infill_extrusion_width");
		optgroup->append_single_option_line("solid_infill_extrusion_width");
		optgroup->append_single_option_line("top_infill_extrusion_width");
		optgroup->append_single_option_line("support_material_extrusion_width");

		optgroup = page->new_optgroup(_(L("Overlap")));
		optgroup->append_single_option_line("infill_overlap");

		optgroup = page->new_optgroup(_(L("Flow")));
		optgroup->append_single_option_line("bridge_flow_ratio");

		optgroup = page->new_optgroup(_(L("Other")));
		optgroup->append_single_option_line("clip_multipart_objects");
		optgroup->append_single_option_line("elefant_foot_compensation");
		optgroup->append_single_option_line("xy_size_compensation");
//		#            optgroup->append_single_option_line("threads");
		optgroup->append_single_option_line("resolution");

	page = add_options_page(_(L("Output options")), "page_white_go.png");
		optgroup = page->new_optgroup(_(L("Sequential printing")));
		optgroup->append_single_option_line("complete_objects");
		line = { _(L("Extruder clearance (mm)")), "" };
		Option option = optgroup->get_option("extruder_clearance_radius");
		option.opt.width = 60;
		line.append_option(option);
		option = optgroup->get_option("extruder_clearance_height");
		option.opt.width = 60;
		line.append_option(option);
		optgroup->append_line(line);

		optgroup = page->new_optgroup(_(L("Output file")));
		optgroup->append_single_option_line("gcode_comments");
		option = optgroup->get_option("output_filename_format");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("Post-processing scripts")), 0);	
		option = optgroup->get_option("post_process");
		option.opt.full_width = true;
		option.opt.height = 50;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Notes")), "note.png");
		optgroup = page->new_optgroup(_(L("Notes")), 0);						
		option = optgroup->get_option("notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Dependencies")), "wrench.png");
		optgroup = page->new_optgroup(_(L("Profile dependencies")));
		line = { _(L("Compatible printers")), "" };
		line.widget = [this](wxWindow* parent){
			return compatible_printers_widget(parent, &m_compatible_printers_checkbox, &m_compatible_printers_btn);
		};
		optgroup->append_line(line, &m_colored_Label);

		option = optgroup->get_option("compatible_printers_condition");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);

		line = Line{ "", "" };
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_parent_preset_description_line);
		};
		optgroup->append_line(line);
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabPrint::reload_config(){
	reload_compatible_printers_widget();
	Tab::reload_config();
}

void TabPrint::update()
{
	Freeze();

	double fill_density = m_config->option<ConfigOptionPercent>("fill_density")->value;

	if (m_config->opt_bool("spiral_vase") &&
		!(m_config->opt_int("perimeters") == 1 && m_config->opt_int("top_solid_layers") == 0 &&
		fill_density == 0)) {
		wxString msg_text = _(L("The Spiral Vase mode requires:\n"
			"- one perimeter\n"
			"- no top solid layers\n"
			"- 0% fill density\n"
			"- no support material\n"
			"- no ensure_vertical_shell_thickness\n"
			"\nShall I adjust those settings in order to enable Spiral Vase?"));
		auto dialog = new wxMessageDialog(parent(), msg_text, _(L("Spiral Vase")), wxICON_WARNING | wxYES | wxNO);
		DynamicPrintConfig new_conf = *m_config;
		if (dialog->ShowModal() == wxID_YES) {
			new_conf.set_key_value("perimeters", new ConfigOptionInt(1));
			new_conf.set_key_value("top_solid_layers", new ConfigOptionInt(0));
			new_conf.set_key_value("fill_density", new ConfigOptionPercent(0));
			new_conf.set_key_value("support_material", new ConfigOptionBool(false));
			new_conf.set_key_value("support_material_enforce_layers", new ConfigOptionInt(0));
			new_conf.set_key_value("ensure_vertical_shell_thickness", new ConfigOptionBool(false));
			fill_density = 0;
		}
		else {
			new_conf.set_key_value("spiral_vase", new ConfigOptionBool(false));
		}
		load_config(new_conf);
		on_value_change("fill_density", fill_density);
	}

	auto first_layer_height = m_config->option<ConfigOptionFloatOrPercent>("first_layer_height")->value;
	auto layer_height = m_config->opt_float("layer_height");
	if (m_config->opt_bool("wipe_tower") &&
		(first_layer_height != 0.2 || layer_height < 0.15 || layer_height > 0.35)) {
		wxString msg_text = _(L("The Wipe Tower currently supports only:\n"
			"- first layer height 0.2mm\n"
			"- layer height from 0.15mm to 0.35mm\n"
			"\nShall I adjust those settings in order to enable the Wipe Tower?"));
		auto dialog = new wxMessageDialog(parent(), msg_text, _(L("Wipe Tower")), wxICON_WARNING | wxYES | wxNO);
		DynamicPrintConfig new_conf = *m_config;
		if (dialog->ShowModal() == wxID_YES) {
			const auto &val = *m_config->option<ConfigOptionFloatOrPercent>("first_layer_height");
			auto percent = val.percent;
			new_conf.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(0.2, percent));

			if (m_config->opt_float("layer_height") < 0.15) new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.15));
			if (m_config->opt_float("layer_height") > 0.35) new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.35));
		}
		else
			new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
		load_config(new_conf);
	}

	if (m_config->opt_bool("wipe_tower") && m_config->opt_bool("support_material") &&
		m_config->opt_float("support_material_contact_distance") > 0. &&
		(m_config->opt_int("support_material_extruder") != 0 || m_config->opt_int("support_material_interface_extruder") != 0)) {
		wxString msg_text = _(L("The Wipe Tower currently supports the non-soluble supports only\n"
			"if they are printed with the current extruder without triggering a tool change.\n"
			"(both support_material_extruder and support_material_interface_extruder need to be set to 0).\n"
			"\nShall I adjust those settings in order to enable the Wipe Tower?"));
		auto dialog = new wxMessageDialog(parent(), msg_text, _(L("Wipe Tower")), wxICON_WARNING | wxYES | wxNO);
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
		wxString msg_text = _(L("For the Wipe Tower to work with the soluble supports, the support layers\n"
			"need to be synchronized with the object layers.\n"
			"\nShall I synchronize support layers in order to enable the Wipe Tower?"));
		auto dialog = new wxMessageDialog(parent(), msg_text, _(L("Wipe Tower")), wxICON_WARNING | wxYES | wxNO);
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
				wxString msg_text = _(L("Supports work better, if the following feature is enabled:\n"
					"- Detect bridging perimeters\n"
					"\nShall I adjust those settings for supports?"));
				auto dialog = new wxMessageDialog(parent(), msg_text, _(L("Support Generator")), wxICON_WARNING | wxYES | wxNO | wxCANCEL);
				DynamicPrintConfig new_conf = *m_config;
				auto answer = dialog->ShowModal();
				if (answer == wxID_YES) {
					// Enable "detect bridging perimeters".
					new_conf.set_key_value("overhangs", new ConfigOptionBool(true));
				} else if (answer == wxID_NO) {
					// Do nothing, leave supports on and "detect bridging perimeters" off.
				} else if (answer == wxID_CANCEL) {
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
		for (auto it : map_names) {
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
				wxString msg_text = _(L("The ")) + str_fill_pattern + _(L(" infill pattern is not supposed to work at 100% density.\n"
					"\nShall I switch to rectilinear fill pattern?"));
				auto dialog = new wxMessageDialog(parent(), msg_text, _(L("Infill")), wxICON_WARNING | wxYES | wxNO);
				DynamicPrintConfig new_conf = *m_config;
				if (dialog->ShowModal() == wxID_YES) {
					new_conf.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
					fill_density = 100;
				}
				else
					fill_density = m_presets->get_selected_preset().config.option<ConfigOptionPercent>("fill_density")->value;
				new_conf.set_key_value("fill_density", new ConfigOptionPercent(fill_density));
				load_config(new_conf);
				on_value_change("fill_density", fill_density);
			}
		}
	}

	bool have_perimeters = m_config->opt_int("perimeters") > 0;
	for (auto el : {"extra_perimeters", "ensure_vertical_shell_thickness", "thin_walls", "overhangs",
					"seam_position", "external_perimeters_first", "external_perimeter_extrusion_width",
					"perimeter_speed", "small_perimeter_speed", "external_perimeter_speed" })
		get_field(el)->toggle(have_perimeters);

	bool have_infill = m_config->option<ConfigOptionPercent>("fill_density")->value > 0;
	// infill_extruder uses the same logic as in Print::extruders()
	for (auto el : {"fill_pattern", "infill_every_layers", "infill_only_where_needed",
					"solid_infill_every_layers", "solid_infill_below_area", "infill_extruder" })
		get_field(el)->toggle(have_infill);

	bool have_solid_infill = m_config->opt_int("top_solid_layers") > 0 || m_config->opt_int("bottom_solid_layers") > 0;
	// solid_infill_extruder uses the same logic as in Print::extruders()
	for (auto el : {"external_fill_pattern", "infill_first", "solid_infill_extruder",
					"solid_infill_extrusion_width", "solid_infill_speed" })
		get_field(el)->toggle(have_solid_infill);

	for (auto el : {"fill_angle", "bridge_angle", "infill_extrusion_width",
					"infill_speed", "bridge_speed" })
		get_field(el)->toggle(have_infill || have_solid_infill);

	get_field("gap_fill_speed")->toggle(have_perimeters && have_infill);

	bool have_top_solid_infill = m_config->opt_int("top_solid_layers") > 0;
	for (auto el : { "top_infill_extrusion_width", "top_solid_infill_speed" })
		get_field(el)->toggle(have_top_solid_infill);

	bool have_default_acceleration = m_config->opt_float("default_acceleration") > 0;
	for (auto el : {"perimeter_acceleration", "infill_acceleration",
					"bridge_acceleration", "first_layer_acceleration" })
		get_field(el)->toggle(have_default_acceleration);

	bool have_skirt = m_config->opt_int("skirts") > 0 || m_config->opt_float("min_skirt_length") > 0;
	for (auto el : { "skirt_distance", "skirt_height" })
		get_field(el)->toggle(have_skirt);

	bool have_brim = m_config->opt_float("brim_width") > 0;
	// perimeter_extruder uses the same logic as in Print::extruders()
	get_field("perimeter_extruder")->toggle(have_perimeters || have_brim);

	bool have_raft = m_config->opt_int("raft_layers") > 0;
	bool have_support_material = m_config->opt_bool("support_material") || have_raft;
	bool have_support_interface = m_config->opt_int("support_material_interface_layers") > 0;
	bool have_support_soluble = have_support_material && m_config->opt_float("support_material_contact_distance") == 0;
	for (auto el : {"support_material_threshold", "support_material_pattern", "support_material_with_sheath",
					"support_material_spacing", "support_material_angle", "support_material_interface_layers",
					"dont_support_bridges", "support_material_extrusion_width", "support_material_contact_distance",
					"support_material_xy_spacing" })
		get_field(el)->toggle(have_support_material);

	for (auto el : {"support_material_interface_spacing", "support_material_interface_extruder",
					"support_material_interface_speed", "support_material_interface_contact_loops" })
		get_field(el)->toggle(have_support_material && have_support_interface);
	get_field("support_material_synchronize_layers")->toggle(have_support_soluble);

	get_field("perimeter_extrusion_width")->toggle(have_perimeters || have_skirt || have_brim);
	get_field("support_material_extruder")->toggle(have_support_material || have_skirt);
	get_field("support_material_speed")->toggle(have_support_material || have_brim || have_skirt);

	bool have_sequential_printing = m_config->opt_bool("complete_objects");
	for (auto el : { "extruder_clearance_radius", "extruder_clearance_height" })
		get_field(el)->toggle(have_sequential_printing);

	bool have_ooze_prevention = m_config->opt_bool("ooze_prevention");
	get_field("standby_temperature_delta")->toggle(have_ooze_prevention);

	bool have_wipe_tower = m_config->opt_bool("wipe_tower");
	for (auto el : { "wipe_tower_x", "wipe_tower_y", "wipe_tower_width", "wipe_tower_rotation_angle", "wipe_tower_bridging"})
		get_field(el)->toggle(have_wipe_tower);

	m_recommended_thin_wall_thickness_description_line->SetText(
		from_u8(PresetHints::recommended_thin_wall_thickness(*m_preset_bundle)));

	Thaw();
}

void TabPrint::OnActivate()
{
	m_recommended_thin_wall_thickness_description_line->SetText(
		from_u8(PresetHints::recommended_thin_wall_thickness(*m_preset_bundle)));
}

void TabFilament::build()
{
	m_presets = &m_preset_bundle->filaments;
	load_initial_data();

	auto page = add_options_page(_(L("Filament")), "spool.png");
		auto optgroup = page->new_optgroup(_(L("Filament")));
		optgroup->append_single_option_line("filament_colour");
		optgroup->append_single_option_line("filament_diameter");
		optgroup->append_single_option_line("extrusion_multiplier");
		optgroup->append_single_option_line("filament_density");
		optgroup->append_single_option_line("filament_cost");

		optgroup = page->new_optgroup(_(L("Temperature ")) +" (\u00B0C)"); // degree sign
		Line line = { _(L("Extruder")), "" };
		line.append_option(optgroup->get_option("first_layer_temperature"));
		line.append_option(optgroup->get_option("temperature"));
		optgroup->append_line(line);

		line = { _(L("Bed")), "" };
		line.append_option(optgroup->get_option("first_layer_bed_temperature"));
		line.append_option(optgroup->get_option("bed_temperature"));
		optgroup->append_line(line);

	page = add_options_page(_(L("Cooling")), "hourglass.png");
		optgroup = page->new_optgroup(_(L("Enable")));
		optgroup->append_single_option_line("fan_always_on");
		optgroup->append_single_option_line("cooling");

		line = { "", "" };
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_cooling_description_line);
		};
		optgroup->append_line(line);

		optgroup = page->new_optgroup(_(L("Fan settings")));
		line = { _(L("Fan speed")), "" };
		line.append_option(optgroup->get_option("min_fan_speed"));
		line.append_option(optgroup->get_option("max_fan_speed"));
		optgroup->append_line(line);

		optgroup->append_single_option_line("bridge_fan_speed");
		optgroup->append_single_option_line("disable_fan_first_layers");

		optgroup = page->new_optgroup(_(L("Cooling thresholds")), 250);
		optgroup->append_single_option_line("fan_below_layer_time");
		optgroup->append_single_option_line("slowdown_below_layer_time");
		optgroup->append_single_option_line("min_print_speed");

	page = add_options_page(_(L("Advanced")), "wrench.png");
		optgroup = page->new_optgroup(_(L("Filament properties")));
		optgroup->append_single_option_line("filament_type");
		optgroup->append_single_option_line("filament_soluble");

		optgroup = page->new_optgroup(_(L("Print speed override")));
		optgroup->append_single_option_line("filament_max_volumetric_speed");

		line = { "", "" };
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_volumetric_speed_description_line);
		};
		optgroup->append_line(line);

        optgroup = page->new_optgroup(_(L("Toolchange behaviour")));
		optgroup->append_single_option_line("filament_loading_speed");
        optgroup->append_single_option_line("filament_unloading_speed");
        optgroup->append_single_option_line("filament_toolchange_delay");
        optgroup->append_single_option_line("filament_cooling_time");
        line = { _(L("Ramming")), "" };
        line.widget = [this](wxWindow* parent){
			auto ramming_dialog_btn = new wxButton(parent, wxID_ANY, _(L("Ramming settings"))+"\u2026", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(ramming_dialog_btn);
            
            ramming_dialog_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent& e)
			{
                RammingDialog dlg(this,(m_config->option<ConfigOptionStrings>("filament_ramming_parameters"))->get_at(0));
                if (dlg.ShowModal() == wxID_OK)
                    (m_config->option<ConfigOptionStrings>("filament_ramming_parameters"))->get_at(0) = dlg.get_parameters();
			}));
			return sizer;
		};
		optgroup->append_line(line);


        page = add_options_page(_(L("Custom G-code")), "cog.png");
		optgroup = page->new_optgroup(_(L("Start G-code")), 0);
		Option option = optgroup->get_option("start_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("End G-code")), 0);
		option = optgroup->get_option("end_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Notes")), "note.png");
		optgroup = page->new_optgroup(_(L("Notes")), 0);
		optgroup->label_width = 0;
		option = optgroup->get_option("filament_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Dependencies")), "wrench.png");
		optgroup = page->new_optgroup(_(L("Profile dependencies")));
		line = { _(L("Compatible printers")), "" };
		line.widget = [this](wxWindow* parent){
			return compatible_printers_widget(parent, &m_compatible_printers_checkbox, &m_compatible_printers_btn);
		};
		optgroup->append_line(line, &m_colored_Label);

		option = optgroup->get_option("compatible_printers_condition");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);

		line = Line{ "", "" };
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_parent_preset_description_line);
		};
		optgroup->append_line(line);
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabFilament::reload_config(){
	reload_compatible_printers_widget();
	Tab::reload_config();
}

void TabFilament::update()
{
	wxString text = from_u8(PresetHints::cooling_description(m_presets->get_edited_preset()));
	m_cooling_description_line->SetText(text);
	text = from_u8(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle));
	m_volumetric_speed_description_line->SetText(text);

	bool cooling = m_config->opt_bool("cooling", 0);
	bool fan_always_on = cooling || m_config->opt_bool("fan_always_on", 0);

	for (auto el : { "max_fan_speed", "fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed" })
		get_field(el)->toggle(cooling);

	for (auto el : { "min_fan_speed", "disable_fan_first_layers" })
		get_field(el)->toggle(fan_always_on);
}

void TabFilament::OnActivate()
{
	m_volumetric_speed_description_line->SetText(from_u8(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle)));
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
	load_initial_data();

	auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"));
	m_initial_extruders_count = m_extruders_count = nozzle_diameter->values.size();
	const Preset* parent_preset = m_presets->get_selected_preset_parent();
	m_sys_extruders_count = parent_preset == nullptr ? 0 :
			static_cast<const ConfigOptionFloats*>(parent_preset->config.option("nozzle_diameter"))->values.size();

	auto page = add_options_page(_(L("General")), "printer_empty.png");
		auto optgroup = page->new_optgroup(_(L("Size and coordinates")));

		Line line{ _(L("Bed shape")), "" };
		line.widget = [this](wxWindow* parent){
			auto btn = new wxButton(parent, wxID_ANY, _(L(" Set "))+"\u2026", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
			//			btn->SetFont(Slic3r::GUI::small_font);
			btn->SetBitmap(wxBitmap(from_u8(Slic3r::var("printer_empty.png")), wxBITMAP_TYPE_PNG));

			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e)
			{
				auto dlg = new BedShapeDialog(this);
				dlg->build_dialog(m_config->option<ConfigOptionPoints>("bed_shape"));
				if (dlg->ShowModal() == wxID_OK){
					load_key_value("bed_shape", dlg->GetValue());
					update_changed_ui();
				}
			}));

			return sizer;
		};
		optgroup->append_line(line, &m_colored_Label);
        optgroup->append_single_option_line("max_print_height");
        optgroup->append_single_option_line("z_offset");

		optgroup = page->new_optgroup(_(L("Capabilities")));
		ConfigOptionDef def;
			def.type =  coInt,
			def.default_value = new ConfigOptionInt(1); 
			def.label = L("Extruders");
			def.tooltip = L("Number of extruders of the printer.");
			def.min = 1;
		Option option(def, "extruders_count");
		optgroup->append_single_option_line(option);
		optgroup->append_single_option_line("single_extruder_multi_material");

		optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value){
			size_t extruders_count = boost::any_cast<int>(optgroup->get_value("extruders_count"));
			wxTheApp->CallAfter([this, opt_key, value, extruders_count](){
				if (opt_key.compare("extruders_count")==0 || opt_key.compare("single_extruder_multi_material")==0) {
					extruders_count_changed(extruders_count);
					update_dirty();
                    if (opt_key.compare("single_extruder_multi_material")==0) // the single_extruder_multimaterial was added to force pages
                        on_value_change(opt_key, value);                      // rebuild - let's make sure the on_value_change is not skipped
				}
				else {
					update_dirty();
					on_value_change(opt_key, value);
				}
			});
		};


		if (!m_no_controller)
		{
		optgroup = page->new_optgroup(_(L("USB/Serial connection")));
			line = {_(L("Serial port")), ""};
			Option serial_port = optgroup->get_option("serial_port");
			serial_port.side_widget = ([this](wxWindow* parent){
				auto btn = new wxBitmapButton(parent, wxID_ANY, wxBitmap(from_u8(Slic3r::var("arrow_rotate_clockwise.png")), wxBITMAP_TYPE_PNG),
					wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
				btn->SetToolTip(_(L("Rescan serial ports")));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent e) {update_serial_ports(); });
				return sizer;
			});
			auto serial_test = [this](wxWindow* parent){
				auto btn = m_serial_test_btn = new wxButton(parent, wxID_ANY,
					_(L("Test")), wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
//				btn->SetFont($Slic3r::GUI::small_font);
				btn->SetBitmap(wxBitmap(from_u8(Slic3r::var("wrench.png")), wxBITMAP_TYPE_PNG));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent e){
					auto sender = new GCodeSender();					
					auto res = sender->connect(
						m_config->opt_string("serial_port"), 
						m_config->opt_int("serial_speed")
						);
					if (res && sender->wait_connected()) {
						show_info(parent, _(L("Connection to printer works correctly.")), _(L("Success!")));
					}
					else {
						show_error(parent, _(L("Connection failed.")));
					}
				});
				return sizer;
			};

			line.append_option(serial_port);
			line.append_option(optgroup->get_option("serial_speed"));
			line.append_widget(serial_test);
			optgroup->append_line(line);
		}

		optgroup = page->new_optgroup(_(L("OctoPrint upload")));

		auto octoprint_host_browse = [this, optgroup] (wxWindow* parent) {
			auto btn = new wxButton(parent, wxID_ANY, _(L(" Browse "))+"\u2026", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
			btn->SetBitmap(wxBitmap(from_u8(Slic3r::var("zoom.png")), wxBITMAP_TYPE_PNG));
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, [this, parent, optgroup](wxCommandEvent e) {
				BonjourDialog dialog(parent);
				if (dialog.show_and_lookup()) {
					optgroup->set_value("octoprint_host", std::move(dialog.get_selected()), true);
				}
			});

			return sizer;
		};

		auto octoprint_host_test = [this](wxWindow* parent) {
			auto btn = m_octoprint_host_test_btn = new wxButton(parent, wxID_ANY, _(L("Test")), 
				wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
			btn->SetBitmap(wxBitmap(from_u8(Slic3r::var("wrench.png")), wxBITMAP_TYPE_PNG));
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent e) {
				OctoPrint octoprint(m_config);
				wxString msg;
				if (octoprint.test(msg)) {
					show_info(this, _(L("Connection to OctoPrint works correctly.")), _(L("Success!")));
				} else {
					const auto text = wxString::Format("%s: %s\n\n%s",
						_(L("Could not connect to OctoPrint")), msg, _(L("Note: OctoPrint version at least 1.1.0 is required."))
					);
					show_error(this, text);
				}
			});

			return sizer;
		};

		Line host_line = optgroup->create_single_option_line("octoprint_host");
		host_line.append_widget(octoprint_host_browse);
		host_line.append_widget(octoprint_host_test);
		optgroup->append_line(host_line);
		optgroup->append_single_option_line("octoprint_apikey");

		if (Http::ca_file_supported()) {

			Line cafile_line = optgroup->create_single_option_line("octoprint_cafile");

			auto octoprint_cafile_browse = [this, optgroup] (wxWindow* parent) {
				auto btn = new wxButton(parent, wxID_ANY, _(L(" Browse "))+"\u2026", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
				btn->SetBitmap(wxBitmap(from_u8(Slic3r::var("zoom.png")), wxBITMAP_TYPE_PNG));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [this, optgroup] (wxCommandEvent e){
					static const auto filemasks = _(L("Certificate files (*.crt, *.pem)|*.crt;*.pem|All files|*.*"));
					wxFileDialog openFileDialog(this, _(L("Open CA certificate file")), "", "", filemasks, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
					if (openFileDialog.ShowModal() != wxID_CANCEL) {
						optgroup->set_value("octoprint_cafile", std::move(openFileDialog.GetPath()), true);
					}
				});

				return sizer;
			};

			cafile_line.append_widget(octoprint_cafile_browse);
			optgroup->append_line(cafile_line);

			auto octoprint_cafile_hint = [this, optgroup] (wxWindow* parent) {
				auto txt = new wxStaticText(parent, wxID_ANY, 
					_(L("HTTPS CA file is optional. It is only needed if you use HTTPS with a self-signed certificate.")));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(txt);
				return sizer;
			};

			Line cafile_hint { "", "" };
			cafile_hint.full_width = 1;
			cafile_hint.widget = std::move(octoprint_cafile_hint);
			optgroup->append_line(cafile_hint);

		}

		optgroup = page->new_optgroup(_(L("Firmware")));
		optgroup->append_single_option_line("gcode_flavor");

		optgroup = page->new_optgroup(_(L("Advanced")));
		optgroup->append_single_option_line("use_relative_e_distances");
		optgroup->append_single_option_line("use_firmware_retraction");
		optgroup->append_single_option_line("use_volumetric_e");
		optgroup->append_single_option_line("variable_layer_height");

	page = add_options_page(_(L("Custom G-code")), "cog.png");
		optgroup = page->new_optgroup(_(L("Start G-code")), 0);
		option = optgroup->get_option("start_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("End G-code")), 0);
		option = optgroup->get_option("end_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("Before layer change G-code")), 0);
		option = optgroup->get_option("before_layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("After layer change G-code")), 0);
		option = optgroup->get_option("layer_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("Tool change G-code")), 0);
		option = optgroup->get_option("toolchange_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("Between objects G-code (for sequential printing)")), 0);
		option = optgroup->get_option("between_objects_gcode");
		option.opt.full_width = true;
		option.opt.height = 150;
		optgroup->append_single_option_line(option);
	
	page = add_options_page(_(L("Notes")), "note.png");
		optgroup = page->new_optgroup(_(L("Notes")), 0);
		option = optgroup->get_option("printer_notes");
		option.opt.full_width = true;
		option.opt.height = 250;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Dependencies")), "wrench.png");
		optgroup = page->new_optgroup(_(L("Profile dependencies")));
		line = Line{ "", "" };
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_parent_preset_description_line);
		};
		optgroup->append_line(line);

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
	reload_config();
	on_value_change("extruders_count", extruders_count);
}

void TabPrinter::build_extruder_pages(){
	for (auto extruder_idx = m_extruder_pages.size(); extruder_idx < m_extruders_count; ++extruder_idx){
		//# build page
		char buf[MIN_BUF_LENGTH_FOR_L];
		sprintf(buf, _CHB(L("Extruder %d")), extruder_idx + 1);
		auto page = add_options_page(from_u8(buf), "funnel.png", true);
		m_extruder_pages.push_back(page);
			
			auto optgroup = page->new_optgroup(_(L("Size")));
			optgroup->append_single_option_line("nozzle_diameter", extruder_idx);
		
			optgroup = page->new_optgroup(_(L("Layer height limits")));
			optgroup->append_single_option_line("min_layer_height", extruder_idx);
			optgroup->append_single_option_line("max_layer_height", extruder_idx);
				
		
			optgroup = page->new_optgroup(_(L("Position (for multi-extruder printers)")));
			optgroup->append_single_option_line("extruder_offset", extruder_idx);
		
			optgroup = page->new_optgroup(_(L("Retraction")));
			optgroup->append_single_option_line("retract_length", extruder_idx);
			optgroup->append_single_option_line("retract_lift", extruder_idx);
				Line line = { _(L("Only lift Z")), "" };
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
	
			optgroup = page->new_optgroup(_(L("Retraction when tool is disabled (advanced settings for multi-extruder setups)")));
			optgroup->append_single_option_line("retract_length_toolchange", extruder_idx);
			optgroup->append_single_option_line("retract_restart_extra_toolchange", extruder_idx);

			optgroup = page->new_optgroup(_(L("Preview")));
			optgroup->append_single_option_line("extruder_colour", extruder_idx);
	}
 
	// # remove extra pages
	if (m_extruders_count <= m_extruder_pages.size()) {
		m_extruder_pages.resize(m_extruders_count);
	}

	// # rebuild page list
	PageShp page_note = m_pages.back();
	m_pages.pop_back();
	while (m_pages.back()->title().find(_(L("Extruder"))) != std::string::npos)
		m_pages.pop_back();
	for (auto page_extruder : m_extruder_pages)
		m_pages.push_back(page_extruder);
	m_pages.push_back(page_note);

    {
        // if we have a single extruder MM setup, add a page with configuration options:
        for (int i=0;i<m_pages.size();++i) // first make sure it's not there already
            if (m_pages[i]->title().find(_(L("Single extruder MM setup"))) != std::string::npos) {
                m_pages.erase(m_pages.begin()+i);
                break;
            }
        if ( m_extruder_pages.size()>1 && m_config->opt_bool("single_extruder_multi_material")) {
            // create a page, but pretend it's an extruder page, so we can add it to m_pages ourselves
            auto page = add_options_page(_(L("Single extruder MM setup")), "printer_empty.png",true);
                auto optgroup = page->new_optgroup(_(L("Single extruder multimaterial parameters")));
                optgroup->append_single_option_line("cooling_tube_retraction");
                optgroup->append_single_option_line("cooling_tube_length");
                optgroup->append_single_option_line("parking_pos_retraction");
            m_pages.insert(m_pages.begin()+1,page);
        }
    }

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

	m_octoprint_host_test_btn->Enable(!m_config->opt_string("octoprint_host").empty());
	
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
				_(L("The Wipe option is not available when using the Firmware Retraction mode.\n"
				"\nShall I disable it in order to enable Firmware Retraction?")),
				_(L("Firmware Retraction")), wxICON_WARNING | wxYES | wxNO);

			DynamicPrintConfig new_conf = *m_config;
			if (dialog->ShowModal() == wxID_YES) {
				auto wipe = static_cast<ConfigOptionBools*>(m_config->option("wipe")->clone());
				for (int w = 0; w < wipe->values.size(); w++)
					wipe->values[w] = false;
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

	preset.is_default ? m_btn_delete_preset->Disable() : m_btn_delete_preset->Enable(true);
	update();
	// For the printer profile, generate the extruder pages.
	on_preset_loaded();
	// Reload preset pages with the new configuration values.
	reload_config();
	const Preset* parent = m_presets->get_selected_preset_parent();
	m_nonsys_btn_icon = parent == nullptr ? "bullet_white.png" : "sys_unlock.png";

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

		if (name() == "print")
			update_frequently_changed_parameters();
		if (m_name == "printer"){
			static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<TabPrinter*>(this)->m_extruders_count;
			const Preset* parent_preset = m_presets->get_selected_preset_parent();
			static_cast<TabPrinter*>(this)->m_sys_extruders_count = parent_preset == nullptr ? 0 :
				static_cast<const ConfigOptionFloats*>(parent_preset->config.option("nozzle_diameter"))->values.size();
		}
		update_sys_ui_after_sel_preset();
		update_full_options_list();
		update_changed_ui();
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
void Tab::select_preset(const std::string& preset_name /*= ""*/)
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
bool Tab::may_discard_current_dirty_preset(PresetCollection* presets /*= nullptr*/, const std::string& new_printer_name /*= ""*/)
{
	if (presets == nullptr) presets = m_presets;
	// Display a dialog showing the dirty options in a human readable form.
	auto old_preset = presets->get_edited_preset();
	auto type_name = presets->name();
	auto tab = "          ";
	auto name = old_preset.is_default ?
		_(L("Default ")) + type_name + _(L(" preset")) :
		(type_name + _(L(" preset\n")) + tab + old_preset.name);
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
		name + _(L("\n\nis not compatible with printer\n")) +tab + new_printer_name+ _(L("\n\nand it has the following unsaved changes:")) :
		name + _(L("\n\nhas the following unsaved changes:"));
	auto confirm = new wxMessageDialog(parent(),
		message + "\n" +changes +_(L("\n\nDiscard changes and continue anyway?")),
		_(L("Unsaved Changes")), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
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
			m_is_nonsys_values = page->m_is_nonsys_values;
			m_is_modified_values = page->m_is_modified_values;
			break;
		}
	if (page == nullptr) return;

	for (auto& el : m_pages)
		el.get()->Hide();
	page->Show();
	m_hsizer->Layout();
	Refresh();

	update_undo_buttons();
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
			if (preset.is_default || preset.is_system || preset.is_external)
				continue;
			values.push_back(preset.name);
		}

		auto dlg = new SavePresetWindow(parent());
		dlg->build(title(), default_name, values);	
		if (dlg->ShowModal() != wxID_OK)
			return;
		name = dlg->get_name();
		if (name == ""){
			show_error(this, _(L("The supplied name is empty. It can't be saved.")));
			return;
		}
		const Preset *existing = m_presets->find_preset(name, false);
		if (existing && (existing->is_default || existing->is_system)) {
			show_error(this, _(L("Cannot overwrite a system profile.")));
			return;
		}
		if (existing && (existing->is_external)) {
			show_error(this, _(L("Cannot overwrite an external.")));
			return;
		}
	}

	// Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
	m_presets->save_current_preset(name);
	// Mark the print & filament enabled if they are compatible with the currently selected preset.
	m_preset_bundle->update_compatible_with_printer(false);
	// Add the new item into the UI component, remove dirty flags and activate the saved item.
	update_tab_ui();
	// Update the selection boxes at the platter.
	on_presets_changed();

	if (m_name == "printer")
		static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<TabPrinter*>(this)->m_extruders_count;
	update_changed_ui();
}

// Called for a currently selected preset.
void Tab::delete_preset()
{
	auto current_preset = m_presets->get_selected_preset();
	// Don't let the user delete the ' - default - ' configuration.
	wxString action = current_preset.is_external ? _(L("remove")) : _(L("delete"));
	wxString msg = _(L("Are you sure you want to ")) + action + _(L(" the selected preset?"));
	action = current_preset.is_external ? _(L("Remove")) : _(L("Delete"));
	wxString title = action + _(L(" Preset"));
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
	m_show_btn_incompatible_presets = get_app_config()->get("show_incompatible_presets")[0] == '1' ? true : false;
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
	*checkbox = new wxCheckBox(parent, wxID_ANY, _(L("All")));
	*btn = new wxButton(parent, wxID_ANY, _(L(" Set "))+"\u2026", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);

	(*btn)->SetBitmap(wxBitmap(from_u8(Slic3r::var("printer_empty.png")), wxBITMAP_TYPE_PNG));

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
		update_changed_ui();
	}) );

	(*btn)->Bind(wxEVT_BUTTON, ([this, parent, checkbox, btn](wxCommandEvent e)
	{
		// # Collect names of non-default non-external printer profiles.
		PresetCollection *printers = &m_preset_bundle->printers;
		wxArrayString presets;
		for (size_t idx = 0; idx < printers->size(); ++idx)
		{
			Preset& preset = printers->preset(idx);
			if (!preset.is_default && !preset.is_external && !preset.is_system)
				presets.Add(preset.name);
		}

		auto dlg = new wxMultiChoiceDialog(parent,
		_(L("Select the printers this profile is compatible with.")),
		_(L("Compatible printers")),  presets);
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
			update_changed_ui();
		}
	}));
	return sizer; 
}

void Tab::update_presetsctrl(wxDataViewTreeCtrl* ui, bool show_incompatible)
{
	if (ui == nullptr)
		return;
	ui->Freeze();
	ui->DeleteAllItems();
	auto presets = m_presets->get_presets();
	auto idx_selected = m_presets->get_idx_selected();
	auto suffix_modified = m_presets->get_suffix_modified();
	int icon_compatible = 0;
	int icon_incompatible = 1;
	int cnt_items = 0;

	auto root_sys = ui->AppendContainer(wxDataViewItem(0), _(L("System presets")));
	auto root_def = ui->AppendContainer(wxDataViewItem(0), _(L("Default presets")));

	auto show_def = get_app_config()->get("no_defaults")[0] != '1';

	for (size_t i = presets.front().is_visible ? 0 : 1; i < presets.size(); ++i) {
		const Preset &preset = presets[i];
		if (!preset.is_visible || (!show_incompatible && !preset.is_compatible && i != idx_selected))
			continue;

		auto preset_name = wxString::FromUTF8((preset.name + (preset.is_dirty ? suffix_modified : "")).c_str());

		wxDataViewItem item;
		if (preset.is_system)
			item = ui->AppendItem(root_sys, preset_name,
			preset.is_compatible ? icon_compatible : icon_incompatible);
		else if (show_def && preset.is_default)
			item = ui->AppendItem(root_def, preset_name,
			preset.is_compatible ? icon_compatible : icon_incompatible);
		else
		{
			auto parent = m_presets->get_preset_parent(preset);
			if (parent == nullptr)
				item = ui->AppendItem(root_def, preset_name,
				preset.is_compatible ? icon_compatible : icon_incompatible);
			else
			{
				auto parent_name = parent->name;

				wxDataViewTreeStoreContainerNode *node = ui->GetStore()->FindContainerNode(root_sys);
				if (node)
				{
					wxDataViewTreeStoreNodeList::iterator iter;
					for (iter = node->GetChildren().begin(); iter != node->GetChildren().end(); iter++)
					{
						wxDataViewTreeStoreNode* child = *iter;
						auto child_item = child->GetItem();
						auto item_text = ui->GetItemText(child_item);
						if (item_text == parent_name)
						{
							auto added_child = ui->AppendItem(child->GetItem(), preset_name,
								preset.is_compatible ? icon_compatible : icon_incompatible);
							if (!added_child){
								ui->DeleteItem(child->GetItem());
								auto new_parent = ui->AppendContainer(root_sys, parent_name,
									preset.is_compatible ? icon_compatible : icon_incompatible);
								ui->AppendItem(new_parent, preset_name,
									preset.is_compatible ? icon_compatible : icon_incompatible);
							}
							break;
						}
					}
				}
			}
		}

		cnt_items++;
		if (i == idx_selected){
			ui->Select(item);
			m_cc_presets_choice->SetText(preset_name);
		}
	}
	if (ui->GetStore()->GetChildCount(root_def) == 0)
		ui->DeleteItem(root_def);

	ui->Thaw();
}

void Tab::update_tab_presets(wxComboCtrl* ui, bool show_incompatible)
{
	if (ui == nullptr)
		return;
	ui->Freeze();
	ui->Clear();
	auto presets = m_presets->get_presets();
	auto idx_selected = m_presets->get_idx_selected();
	auto suffix_modified = m_presets->get_suffix_modified();
	int icon_compatible = 0;
	int icon_incompatible = 1;
	int cnt_items = 0;

	wxDataViewTreeCtrlComboPopup* popup = wxDynamicCast(m_cc_presets_choice->GetPopupControl(), wxDataViewTreeCtrlComboPopup);
	if (popup != nullptr)
	{
		popup->DeleteAllItems();

		auto root_sys = popup->AppendContainer(wxDataViewItem(0), _(L("System presets")));
		auto root_def = popup->AppendContainer(wxDataViewItem(0), _(L("Default presets")));

		auto show_def = get_app_config()->get("no_defaults")[0] != '1';

		for (size_t i = presets.front().is_visible ? 0 : 1; i < presets.size(); ++i) {
			const Preset &preset = presets[i];
			if (!preset.is_visible || (!show_incompatible && !preset.is_compatible && i != idx_selected))
				continue;

			auto preset_name = wxString::FromUTF8((preset.name + (preset.is_dirty ? suffix_modified : "")).c_str());

			wxDataViewItem item;
			if (preset.is_system)
				item = popup->AppendItem(root_sys, preset_name, 
										 preset.is_compatible ? icon_compatible : icon_incompatible);
			else if (show_def && preset.is_default)
				item = popup->AppendItem(root_def, preset_name, 
										 preset.is_compatible ? icon_compatible : icon_incompatible);
			else 
			{
				auto parent = m_presets->get_preset_parent(preset);
				if (parent == nullptr)
					item = popup->AppendItem(root_def, preset_name,
											 preset.is_compatible ? icon_compatible : icon_incompatible);
				else
				{
					auto parent_name = parent->name;

					wxDataViewTreeStoreContainerNode *node = popup->GetStore()->FindContainerNode(root_sys);
					if (node) 
					{
						wxDataViewTreeStoreNodeList::iterator iter;
						for (iter = node->GetChildren().begin(); iter != node->GetChildren().end(); iter++)
						{
							wxDataViewTreeStoreNode* child = *iter;
							auto child_item = child->GetItem();
							auto item_text = popup->GetItemText(child_item);
							if (item_text == parent_name)
							{
								auto added_child = popup->AppendItem(child->GetItem(), preset_name,
									preset.is_compatible ? icon_compatible : icon_incompatible);
								if (!added_child){
									popup->DeleteItem(child->GetItem());
									auto new_parent = popup->AppendContainer(root_sys, parent_name,
										preset.is_compatible ? icon_compatible : icon_incompatible);
									popup->AppendItem(new_parent, preset_name,
										preset.is_compatible ? icon_compatible : icon_incompatible);
								}
								break;
							}
						}
					}
				}
			}

			cnt_items++;
			if (i == idx_selected){
				popup->Select(item);
				m_cc_presets_choice->SetText(preset_name);
			}
		}
		if (popup->GetStore()->GetChildCount(root_def) == 0)
			popup->DeleteItem(root_def);
	}
	ui->Thaw();
}

void Page::reload_config()
{
	for (auto group : m_optgroups)
		group->reload_config();
}

Field* Page::get_field(const t_config_option_key& opt_key, int opt_index /*= -1*/) const
{
	Field* field = nullptr;
	for (auto opt : m_optgroups){
		field = opt->get_fieldc(opt_key, opt_index);
		if (field != nullptr)
			return field;
	}
	return field;
}

bool Page::set_value(const t_config_option_key& opt_key, const boost::any& value){
	bool changed = false;
	for(auto optgroup: m_optgroups) {
		if (optgroup->set_value(opt_key, value))
			changed = 1 ;
	}
	return changed;
}

// package Slic3r::GUI::Tab::Page;
ConfigOptionsGroupShp Page::new_optgroup(const wxString& title, int noncommon_label_width /*= -1*/)
{
	//! config_ have to be "right"
	ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(this, title, m_config, true);
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

	optgroup->m_get_initial_config = [this](){
		DynamicPrintConfig config = static_cast<Tab*>(GetParent())->m_presets->get_selected_preset().config;
		return config;
	};

	optgroup->m_get_sys_config = [this](){
		DynamicPrintConfig config = static_cast<Tab*>(GetParent())->m_presets->get_selected_preset_parent()->config;
		return config;
	};

	optgroup->have_sys_config = [this](){
		return static_cast<Tab*>(GetParent())->m_presets->get_selected_preset_parent() != nullptr;
	};

	optgroup->nonsys_btn_icon = [this](){
		return static_cast<Tab*>(GetParent())->m_nonsys_btn_icon;
	};

	vsizer()->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 10);
	m_optgroups.push_back(optgroup);

	return optgroup;
}

void SavePresetWindow::build(const wxString& title, const std::string& default_name, std::vector<std::string> &values)
{
	auto text = new wxStaticText(this, wxID_ANY, _(L("Save ")) + title + _(L(" as:")), 
									wxDefaultPosition, wxDefaultSize);
	m_combo = new wxComboBox(this, wxID_ANY, from_u8(default_name), 
							wxDefaultPosition, wxDefaultSize, 0, 0, wxTE_PROCESS_ENTER);
	for (auto value : values)
		m_combo->Append(from_u8(value));
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
			show_error(this, _(L("The supplied name is not valid; the following characters are not allowed:"))+" <>:/\\|?*\"");
		}
		else if (m_chosen_name.compare("- default -") == 0) {
			show_error(this, _(L("The supplied name is not available.")));
		}
		else {
			EndModal(wxID_OK);
		}
	}
}

} // GUI
} // Slic3r
