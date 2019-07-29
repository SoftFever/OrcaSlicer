// #include "libslic3r/GCodeSender.hpp"
#include "slic3r/Utils/Serial.hpp"
#include "Tab.hpp"
#include "PresetBundle.hpp"
#include "PresetHints.hpp"
#include "libslic3r/Utils.hpp"

#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "BonjourDialog.hpp"
#include "WipeTowerDialog.hpp"
#include "ButtonsDescription.hpp"

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
#include <wx/wupdlock.h>

#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "ConfigWizard.hpp"

namespace Slic3r {
namespace GUI {


wxDEFINE_EVENT(EVT_TAB_VALUE_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_TAB_PRESETS_CHANGED, SimpleEvent);

// Tab::Tab(wxNotebook* parent, const wxString& title, const char* name) : 
// 	m_parent(parent), m_title(title), m_name(name)
Tab::Tab(wxNotebook* parent, const wxString& title, Preset::Type type) :
	m_parent(parent), m_title(title), m_type(type)
{
	Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL/*, name*/);
	this->SetFont(Slic3r::GUI::wxGetApp().normal_font());

	m_compatible_printers.type			= Preset::TYPE_PRINTER;
	m_compatible_printers.key_list		= "compatible_printers";
	m_compatible_printers.key_condition	= "compatible_printers_condition";
	m_compatible_printers.dialog_title 	= _(L("Compatible printers"));
	m_compatible_printers.dialog_label 	= _(L("Select the printers this profile is compatible with."));

	m_compatible_prints.type			= Preset::TYPE_PRINT;
   	m_compatible_prints.key_list 		= "compatible_prints";
	m_compatible_prints.key_condition	= "compatible_prints_condition";
	m_compatible_prints.dialog_title 	= _(L("Compatible print profiles"));
	m_compatible_prints.dialog_label 	= _(L("Select the print profiles this profile is compatible with."));

	wxGetApp().tabs_list.push_back(this);

    m_em_unit = wxGetApp().em_unit();

	Bind(wxEVT_SIZE, ([this](wxSizeEvent &evt) {
		for (auto page : m_pages)
			if (! page.get()->IsShown())
				page->layout_valid = false;
		evt.Skip();
	}));
}

void Tab::set_type()
{
    if (m_name == "print")              { m_type = Slic3r::Preset::TYPE_PRINT; }
    else if (m_name == "sla_print")     { m_type = Slic3r::Preset::TYPE_SLA_PRINT; }
    else if (m_name == "filament")      { m_type = Slic3r::Preset::TYPE_FILAMENT; }
    else if (m_name == "sla_material")  { m_type = Slic3r::Preset::TYPE_SLA_MATERIAL; }
    else if (m_name == "printer")       { m_type = Slic3r::Preset::TYPE_PRINTER; }
    else                                { m_type = Slic3r::Preset::TYPE_INVALID; assert(false); }
}

// sub new
void Tab::create_preset_tab()
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_preset_bundle = wxGetApp().preset_bundle;

	// Vertical sizer to hold the choice menu and the rest of the page.
#ifdef __WXOSX__
	auto  *main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->SetSizeHints(this);
	this->SetSizer(main_sizer);

	// Create additional panel to Fit() it from OnActivate()
	// It's needed for tooltip showing on OSX
	m_tmp_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
	auto panel = m_tmp_panel; 
	auto  sizer = new wxBoxSizer(wxVERTICAL);
	m_tmp_panel->SetSizer(sizer);
	m_tmp_panel->Layout();

	main_sizer->Add(m_tmp_panel, 1, wxEXPAND | wxALL, 0);
#else
	Tab *panel = this;
	auto  *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->SetSizeHints(panel);
	panel->SetSizer(sizer);
#endif //__WXOSX__

	// preset chooser
    m_presets_choice = new wxBitmapComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(35 * m_em_unit, -1), 0, 0, wxCB_READONLY);

	auto color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);

	//buttons
    m_scaled_buttons.reserve(6);
    m_scaled_buttons.reserve(2);

    add_scaled_button(panel, &m_btn_save_preset, "save");
    add_scaled_button(panel, &m_btn_delete_preset, "cross");

	m_show_incompatible_presets = false;
	add_scaled_bitmap(this, m_bmp_show_incompatible_presets, "flag_red");
	add_scaled_bitmap(this, m_bmp_hide_incompatible_presets, "flag_green");

    add_scaled_button(panel, &m_btn_hide_incompatible_presets, m_bmp_hide_incompatible_presets.name());

    // TRN "Save current Settings"
	m_btn_save_preset->SetToolTip(wxString::Format(_(L("Save current %s")),m_title));
	m_btn_delete_preset->SetToolTip(_(L("Delete this preset")));
	m_btn_delete_preset->Disable();

    add_scaled_button(panel, &m_question_btn, "question");

	m_question_btn->SetToolTip(_(L("Hover the cursor over buttons to find more information \n"
								   "or click this button.")));

	// Determine the theme color of OS (dark or light)
    auto luma = wxGetApp().get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	// Bitmaps to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
	add_scaled_bitmap(this, m_bmp_value_lock  , luma >= 128 ? "lock_closed" : "lock_closed_white");
	add_scaled_bitmap(this, m_bmp_value_unlock, "lock_open");
	m_bmp_non_system = &m_bmp_white_bullet;
	// Bitmaps to be shown on the "Undo user changes" button next to each input field.
	add_scaled_bitmap(this, m_bmp_value_revert, "undo");
	add_scaled_bitmap(this, m_bmp_white_bullet, luma >= 128 ? "dot" : "dot_white");

	fill_icon_descriptions();
	set_tooltips_text();

    add_scaled_button(panel, &m_undo_btn,        m_bmp_white_bullet.name());
    add_scaled_button(panel, &m_undo_to_sys_btn, m_bmp_white_bullet.name());

	m_undo_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent) { on_roll_back_value(); }));
	m_undo_to_sys_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent) { on_roll_back_value(true); }));
	m_question_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent)
	{
		auto dlg = new ButtonsDescription(this, m_icon_descriptions);
		if (dlg->ShowModal() == wxID_OK) {
			// Colors for ui "decoration"
            for (Tab *tab : wxGetApp().tabs_list) {
                tab->m_sys_label_clr = wxGetApp().get_label_clr_sys();
                tab->m_modified_label_clr = wxGetApp().get_label_clr_modified();
				tab->update_labels_colour();
			}
		}
	}));

	// Colors for ui "decoration"
	m_sys_label_clr			= wxGetApp().get_label_clr_sys();
	m_modified_label_clr	= wxGetApp().get_label_clr_modified();
	m_default_text_clr		= wxGetApp().get_label_clr_default();

    // Sizer with buttons for mode changing
    m_mode_sizer = new ModeSizer(panel);

    const float scale_factor = /*wxGetApp().*/em_unit(this)*0.1;// GetContentScaleFactor();
	m_hsizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_hsizer, 0, wxEXPAND | wxBOTTOM, 3);
	m_hsizer->Add(m_presets_choice, 0, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
	m_hsizer->AddSpacer(int(4*scale_factor));
	m_hsizer->Add(m_btn_save_preset, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(4 * scale_factor));
	m_hsizer->Add(m_btn_delete_preset, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(16 * scale_factor));
	m_hsizer->Add(m_btn_hide_incompatible_presets, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(64 * scale_factor));
	m_hsizer->Add(m_undo_to_sys_btn, 0, wxALIGN_CENTER_VERTICAL);
	m_hsizer->Add(m_undo_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(32 * scale_factor));
	m_hsizer->Add(m_question_btn, 0, wxALIGN_CENTER_VERTICAL);
    // m_hsizer->AddStretchSpacer(32);
    // StretchSpacer has a strange behavior under OSX, so 
    // There is used just additional sizer for m_mode_sizer with right alignment
    auto mode_sizer = new wxBoxSizer(wxVERTICAL);
    mode_sizer->Add(m_mode_sizer, 1, wxALIGN_RIGHT);
    m_hsizer->Add(mode_sizer, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, wxOSX ? 15 : 10);

	//Horizontal sizer to hold the tree and the selected page.
	m_hsizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_hsizer, 1, wxEXPAND, 0);

	//left vertical sizer
	m_left_sizer = new wxBoxSizer(wxVERTICAL);
	m_hsizer->Add(m_left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);

	// tree
    m_treectrl = new wxTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(20 * m_em_unit, -1),
		wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN | wxWANTS_CHARS);
	m_left_sizer->Add(m_treectrl, 1, wxEXPAND);
    const int img_sz = int(16 * scale_factor + 0.5f);
    m_icons = new wxImageList(img_sz, img_sz, true, 1);
	// Index of the last icon inserted into $self->{icons}.
	m_icon_count = -1;
	m_treectrl->AssignImageList(m_icons);
	m_treectrl->AddRoot("root");
	m_treectrl->SetIndent(0);
	m_disable_tree_sel_changed_event = 0;

	m_treectrl->Bind(wxEVT_TREE_SEL_CHANGED, &Tab::OnTreeSelChange, this);
	m_treectrl->Bind(wxEVT_KEY_DOWN, &Tab::OnKeyDown, this);

	m_presets_choice->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent e) {
		//! Because of The MSW and GTK version of wxBitmapComboBox derived from wxComboBox, 
		//! but the OSX version derived from wxOwnerDrawnCombo, instead of:
		//! select_preset(m_presets_choice->GetStringSelection().ToUTF8().data()); 
		//! we doing next:
		int selected_item = m_presets_choice->GetSelection();
		if (m_selected_preset_item == selected_item && !m_presets->current_is_dirty())
			return;
		if (selected_item >= 0) {
			std::string selected_string = m_presets_choice->GetString(selected_item).ToUTF8().data();
			if (selected_string.find(PresetCollection::separator_head()) == 0
				/*selected_string == "------- System presets -------" ||
				selected_string == "-------  User presets  -------"*/) {
				m_presets_choice->SetSelection(m_selected_preset_item);
				if (wxString::FromUTF8(selected_string.c_str()) == PresetCollection::separator(L("Add a new printer")))
					wxTheApp->CallAfter([]() { Slic3r::GUI::config_wizard(Slic3r::GUI::ConfigWizard::RR_USER); });
				return;
			}
			m_selected_preset_item = selected_item;
			select_preset(selected_string);
		}
	}));

	m_btn_save_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { save_preset(); }));
	m_btn_delete_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { delete_preset(); }));
	m_btn_hide_incompatible_presets->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) {
		toggle_show_hide_incompatible();
	}));

    // Fill cache for mode bitmaps
    m_mode_bitmap_cache.reserve(3);
    m_mode_bitmap_cache.push_back(ScalableBitmap(this, "mode_simple_.png"));
    m_mode_bitmap_cache.push_back(ScalableBitmap(this, "mode_advanced_.png"));
    m_mode_bitmap_cache.push_back(ScalableBitmap(this, "mode_expert_.png"));

	// Initialize the DynamicPrintConfig by default keys/values.
	build();
	rebuild_page_tree();
    m_complited = true;
}

void Tab::add_scaled_button(wxWindow* parent, 
                            ScalableButton** btn, 
                            const std::string& icon_name, 
                            const wxString& label/* = wxEmptyString*/, 
                            long style /*= wxBU_EXACTFIT | wxNO_BORDER*/)
{
    *btn = new ScalableButton(parent, wxID_ANY, icon_name, label, wxDefaultSize, wxDefaultPosition, style);
    m_scaled_buttons.push_back(*btn);
}

void Tab::add_scaled_bitmap(wxWindow* parent, 
                            ScalableBitmap& bmp, 
                            const std::string& icon_name)
{
    bmp = ScalableBitmap(parent, icon_name);
    m_scaled_bitmaps.push_back(&bmp);
}

void Tab::load_initial_data()
{
	m_config = &m_presets->get_edited_preset().config;
	bool has_parent = m_presets->get_selected_preset_parent() != nullptr;
	m_bmp_non_system = has_parent ? &m_bmp_value_unlock : &m_bmp_white_bullet;
	m_ttg_non_system = has_parent ? &m_ttg_value_unlock : &m_ttg_white_bullet_ns;
	m_tt_non_system  = has_parent ? &m_tt_value_unlock  : &m_ttg_white_bullet_ns;
}

Slic3r::GUI::PageShp Tab::add_options_page(const wxString& title, const std::string& icon, bool is_extruder_pages /*= false*/)
{
	// Index of icon in an icon list $self->{icons}.
	auto icon_idx = 0;
	if (!icon.empty()) {
		icon_idx = (m_icon_index.find(icon) == m_icon_index.end()) ? -1 : m_icon_index.at(icon);
		if (icon_idx == -1) {
			// Add a new icon to the icon list.
            m_scaled_icons_list.push_back(ScalableBitmap(this, icon));
            m_icons->Add(m_scaled_icons_list.back().bmp());
            icon_idx = ++m_icon_count;
			m_icon_index[icon] = icon_idx;
		}
	}
	// Initialize the page.
#ifdef __WXOSX__
	auto panel = m_tmp_panel;
#else
	auto panel = this;
#endif
	PageShp page(new Page(panel, title, icon_idx, m_mode_bitmap_cache));
//	page->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#ifdef __WINDOWS__
//	page->SetDoubleBuffered(true);
#endif //__WINDOWS__

	page->SetScrollbars(1, 20, 1, 2);
	page->Hide();
	m_hsizer->Add(page.get(), 1, wxEXPAND | wxLEFT, 5);

    if (!is_extruder_pages) 
		m_pages.push_back(page);

	page->set_config(m_config);
	return page;
}

void Tab::OnActivate()
{
#ifdef __WXOSX__	
	wxWindowUpdateLocker noUpdates(this);

	auto size = GetSizer()->GetSize();
	m_tmp_panel->GetSizer()->SetMinSize(size.x + m_size_move, size.y);
	Fit();
	m_size_move *= -1;
#endif // __WXOSX__
}

void Tab::update_labels_colour()
{
//	Freeze();
	//update options "decoration"
	for (const auto opt : m_options_list)
	{
		const wxColour *color = &m_sys_label_clr;

		// value isn't equal to system value
		if ((opt.second & osSystemValue) == 0) {
			// value is equal to last saved
			if ((opt.second & osInitValue) != 0)
				color = &m_default_text_clr;
			// value is modified
			else
				color = &m_modified_label_clr;
		}
		if (opt.first == "bed_shape" || opt.first == "compatible_prints" || opt.first == "compatible_printers") {
			if (m_colored_Label != nullptr)	{
				m_colored_Label->SetForegroundColour(*color);
				m_colored_Label->Refresh(true);
			}
			continue;
		}

		Field* field = get_field(opt.first);
		if (field == nullptr) continue;
		field->set_label_colour_force(color);
	}
//	Thaw();

	auto cur_item = m_treectrl->GetFirstVisibleItem();
	while (cur_item) {
		auto title = m_treectrl->GetItemText(cur_item);
		for (auto page : m_pages)
		{
			if (page->title() != title)
				continue;
			
			const wxColor *clr = !page->m_is_nonsys_values ? &m_sys_label_clr :
				page->m_is_modified_values ? &m_modified_label_clr :
				&m_default_text_clr;

			m_treectrl->SetItemTextColour(cur_item, *clr);
			break;
		}
		cur_item = m_treectrl->GetNextVisible(cur_item);
	}
}

// Update UI according to changes
void Tab::update_changed_ui()
{
	if (m_postpone_update_ui) 
		return;

	const bool deep_compare = (m_type == Slic3r::Preset::TYPE_PRINTER || m_type == Slic3r::Preset::TYPE_SLA_MATERIAL);
	auto dirty_options = m_presets->current_dirty_options(deep_compare);
	auto nonsys_options = m_presets->current_different_from_parent_options(deep_compare);
    if (m_type == Slic3r::Preset::TYPE_PRINTER) {
		TabPrinter* tab = static_cast<TabPrinter*>(this);
		if (tab->m_initial_extruders_count != tab->m_extruders_count)
			dirty_options.emplace_back("extruders_count");
		if (tab->m_sys_extruders_count != tab->m_extruders_count)
			nonsys_options.emplace_back("extruders_count");
	}

	for (auto& it : m_options_list)
		it.second = m_opt_status_value;

	for (auto opt_key : dirty_options)	m_options_list[opt_key] &= ~osInitValue;
	for (auto opt_key : nonsys_options)	m_options_list[opt_key] &= ~osSystemValue;

//	Freeze();
	//update options "decoration"
	for (const auto opt : m_options_list)
	{
		bool is_nonsys_value = false;
		bool is_modified_value = true;
		const ScalableBitmap *sys_icon =	&m_bmp_value_lock;
		const ScalableBitmap *icon =		&m_bmp_value_revert;

		const wxColour *color =		m_is_default_preset ? &m_default_text_clr : &m_sys_label_clr;

		const wxString *sys_tt =	&m_tt_value_lock;
		const wxString *tt =		&m_tt_value_revert;

		// value isn't equal to system value
		if ((opt.second & osSystemValue) == 0) {
			is_nonsys_value = true;
			sys_icon = m_bmp_non_system;
			sys_tt = m_tt_non_system;
			// value is equal to last saved
			if ((opt.second & osInitValue) != 0)
				color = &m_default_text_clr;
			// value is modified
			else
				color = &m_modified_label_clr;
		}
		if ((opt.second & osInitValue) != 0)
		{
			is_modified_value = false;
			icon = &m_bmp_white_bullet;
			tt = &m_tt_white_bullet;
		}
		if (opt.first == "bed_shape" || opt.first == "compatible_prints" || opt.first == "compatible_printers") {
			if (m_colored_Label != nullptr)	{
				m_colored_Label->SetForegroundColour(*color);
				m_colored_Label->Refresh(true);
			}
			continue;
		}

		Field* field = get_field(opt.first);
		if (field == nullptr) continue;
		field->m_is_nonsys_value = is_nonsys_value;
		field->m_is_modified_value = is_modified_value;
		field->set_undo_bitmap(icon);
		field->set_undo_to_sys_bitmap(sys_icon);
		field->set_undo_tooltip(tt);
		field->set_undo_to_sys_tooltip(sys_tt);
		field->set_label_colour(color);
	}
//	Thaw();

	wxTheApp->CallAfter([this]() {
        if (parent()) //To avoid a crash, parent should be exist for a moment of a tree updating
		    update_changed_tree_ui();
	});
}

void Tab::init_options_list()
{
	if (!m_options_list.empty())
		m_options_list.clear();

	for (const auto opt_key : m_config->keys())
		m_options_list.emplace(opt_key, m_opt_status_value);
}

template<class T>
void add_correct_opts_to_options_list(const std::string &opt_key, std::map<std::string, int>& map, Tab *tab, const int& value)
{
	T *opt_cur = static_cast<T*>(tab->m_config->option(opt_key));
	for (int i = 0; i < opt_cur->values.size(); i++)
		map.emplace(opt_key + "#" + std::to_string(i), value);
}

void TabPrinter::init_options_list()
{
	if (!m_options_list.empty())
		m_options_list.clear();

	for (const auto opt_key : m_config->keys())
	{
		if (opt_key == "bed_shape") {
			m_options_list.emplace(opt_key, m_opt_status_value);
			continue;
		}
		switch (m_config->option(opt_key)->type())
		{
		case coInts:	add_correct_opts_to_options_list<ConfigOptionInts		>(opt_key, m_options_list, this, m_opt_status_value);	break;
		case coBools:	add_correct_opts_to_options_list<ConfigOptionBools		>(opt_key, m_options_list, this, m_opt_status_value);	break;
		case coFloats:	add_correct_opts_to_options_list<ConfigOptionFloats		>(opt_key, m_options_list, this, m_opt_status_value);	break;
		case coStrings:	add_correct_opts_to_options_list<ConfigOptionStrings	>(opt_key, m_options_list, this, m_opt_status_value);	break;
		case coPercents:add_correct_opts_to_options_list<ConfigOptionPercents	>(opt_key, m_options_list, this, m_opt_status_value);	break;
		case coPoints:	add_correct_opts_to_options_list<ConfigOptionPoints		>(opt_key, m_options_list, this, m_opt_status_value);	break;
		default:		m_options_list.emplace(opt_key, m_opt_status_value);		break;
		}
	}
	m_options_list.emplace("extruders_count", m_opt_status_value);
}

void TabSLAMaterial::init_options_list()
{
    if (!m_options_list.empty())
        m_options_list.clear();

    for (const auto opt_key : m_config->keys())
    {
        if (opt_key == "compatible_prints" || opt_key == "compatible_printers") {
            m_options_list.emplace(opt_key, m_opt_status_value);
            continue;
        }
        switch (m_config->option(opt_key)->type())
        {
        case coInts:	add_correct_opts_to_options_list<ConfigOptionInts		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coBools:	add_correct_opts_to_options_list<ConfigOptionBools		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coFloats:	add_correct_opts_to_options_list<ConfigOptionFloats		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coStrings:	add_correct_opts_to_options_list<ConfigOptionStrings	>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coPercents:add_correct_opts_to_options_list<ConfigOptionPercents	>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coPoints:	add_correct_opts_to_options_list<ConfigOptionPoints		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        default:		m_options_list.emplace(opt_key, m_opt_status_value);		break;
        }
    }
}

void Tab::get_sys_and_mod_flags(const std::string& opt_key, bool& sys_page, bool& modified_page)
{
	auto opt = m_options_list.find(opt_key);
	if (sys_page) sys_page = (opt->second & osSystemValue) != 0;
	modified_page |= (opt->second & osInitValue) == 0;
}

void Tab::update_changed_tree_ui()
{
	if (m_options_list.empty())
        return;
	auto cur_item = m_treectrl->GetFirstVisibleItem();
    if (!cur_item || !m_treectrl->IsVisible(cur_item))
        return;

	auto selected_item = m_treectrl->GetSelection();
	auto selection = selected_item ? m_treectrl->GetItemText(selected_item) : "";

	while (cur_item) {
		auto title = m_treectrl->GetItemText(cur_item);
		for (auto page : m_pages)
		{
			if (page->title() != title) 
				continue;
			bool sys_page = true;
			bool modified_page = false;
			if (title == _("General")) {
				std::initializer_list<const char*> optional_keys{ "extruders_count", "bed_shape" };
				for (auto &opt_key : optional_keys) {
					get_sys_and_mod_flags(opt_key, sys_page, modified_page);
				}
			}
			if (title == _("Dependencies")) {
				if (m_type == Slic3r::Preset::TYPE_PRINTER) {
					sys_page = m_presets->get_selected_preset_parent() != nullptr;
					modified_page = false;
				} else {
					if (m_type == Slic3r::Preset::TYPE_FILAMENT || m_type == Slic3r::Preset::TYPE_SLA_MATERIAL)
						get_sys_and_mod_flags("compatible_prints", sys_page, modified_page);
					get_sys_and_mod_flags("compatible_printers", sys_page, modified_page);
				}
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

			const wxColor *clr = sys_page		?	(m_is_default_preset ? &m_default_text_clr : &m_sys_label_clr) :
								 modified_page	?	&m_modified_label_clr : 
													&m_default_text_clr;

			if (page->set_item_colour(clr))
				m_treectrl->SetItemTextColour(cur_item, *clr);

			page->m_is_nonsys_values = !sys_page;
			page->m_is_modified_values = modified_page;

			if (selection == title) {
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
	m_undo_btn->        SetBitmap_(m_is_modified_values ? m_bmp_value_revert: m_bmp_white_bullet);
	m_undo_to_sys_btn-> SetBitmap_(m_is_nonsys_values   ? *m_bmp_non_system : m_bmp_value_lock);

	m_undo_btn->SetToolTip(m_is_modified_values ? m_ttg_value_revert : m_ttg_white_bullet);
	m_undo_to_sys_btn->SetToolTip(m_is_nonsys_values ? *m_ttg_non_system : m_ttg_value_lock);
}

void Tab::on_roll_back_value(const bool to_sys /*= true*/)
{
	int os;
	if (to_sys)	{
		if (!m_is_nonsys_values) return;
		os = osSystemValue;
	}
	else {
		if (!m_is_modified_values) return;
		os = osInitValue;
	}

	m_postpone_update_ui = true;

	auto selection = m_treectrl->GetItemText(m_treectrl->GetSelection());
	for (auto page : m_pages)
		if (page->title() == selection)	{
			for (auto group : page->m_optgroups) {
				if (group->title == _("Capabilities")) {
					if ((m_options_list["extruders_count"] & os) == 0)
						to_sys ? group->back_to_sys_value("extruders_count") : group->back_to_initial_value("extruders_count");
				}
				if (group->title == _("Size and coordinates")) {
					if ((m_options_list["bed_shape"] & os) == 0) {
						to_sys ? group->back_to_sys_value("bed_shape") : group->back_to_initial_value("bed_shape");
						load_key_value("bed_shape", true/*some value*/, true);
					}

				}
				if (group->title == _("Profile dependencies")) {
					if (m_type != Slic3r::Preset::TYPE_PRINTER && (m_options_list["compatible_printers"] & os) == 0) {
						to_sys ? group->back_to_sys_value("compatible_printers") : group->back_to_initial_value("compatible_printers");
						load_key_value("compatible_printers", true/*some value*/, true);

						bool is_empty = m_config->option<ConfigOptionStrings>("compatible_printers")->values.empty();
						m_compatible_printers.checkbox->SetValue(is_empty);
						is_empty ? m_compatible_printers.btn->Disable() : m_compatible_printers.btn->Enable();
					}
					if ((m_type == Slic3r::Preset::TYPE_PRINT || m_type == Slic3r::Preset::TYPE_SLA_PRINT) && (m_options_list["compatible_prints"] & os) == 0) {
						to_sys ? group->back_to_sys_value("compatible_prints") : group->back_to_initial_value("compatible_prints");
						load_key_value("compatible_prints", true/*some value*/, true);

						bool is_empty = m_config->option<ConfigOptionStrings>("compatible_prints")->values.empty();
						m_compatible_prints.checkbox->SetValue(is_empty);
						is_empty ? m_compatible_prints.btn->Disable() : m_compatible_prints.btn->Enable();
					}
				}
				for (auto kvp : group->m_opt_map) {
					const std::string& opt_key = kvp.first;
					if ((m_options_list[opt_key] & os) == 0)
						to_sys ? group->back_to_sys_value(opt_key) : group->back_to_initial_value(opt_key);
				}
			}
			break;
		}

	m_postpone_update_ui = false;
	update_changed_ui();
}

// Update the combo box label of the selected preset based on its "dirty" state,
// comparing the selected preset config with $self->{config}.
void Tab::update_dirty()
{
	m_presets->update_dirty_ui(m_presets_choice);
	on_presets_changed();	
	update_changed_ui();
}

void Tab::update_tab_ui()
{
	m_selected_preset_item = m_presets->update_tab_ui(m_presets_choice, m_show_incompatible_presets, m_em_unit);
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
void Tab::reload_config()
{
//	Freeze();
	for (auto page : m_pages)
		page->reload_config();
// 	Thaw();
}

void Tab::update_mode()
{
    m_mode = wxGetApp().get_mode();

    // update mode for ModeSizer
    m_mode_sizer->SetMode(m_mode);

    update_visibility();
}

void Tab::update_visibility()
{
    Freeze(); // There is needed Freeze/Thaw to avoid a flashing after Show/Layout

	for (auto page : m_pages)
        page->update_visibility(m_mode);
    update_page_tree_visibility();

    Layout();
	Thaw();

    update_changed_tree_ui();
}

void Tab::msw_rescale()
{
    m_em_unit = wxGetApp().em_unit();

    m_mode_sizer->msw_rescale();

    m_presets_choice->SetSize(35 * m_em_unit, -1);
    m_treectrl->SetMinSize(wxSize(20 * m_em_unit, -1));

    update_tab_ui();

    // rescale buttons and cached bitmaps
    for (const auto btn : m_scaled_buttons)
        btn->msw_rescale();
    for (const auto bmp : m_scaled_bitmaps)
        bmp->msw_rescale();
    for (ScalableBitmap& bmp : m_mode_bitmap_cache)
        bmp.msw_rescale();

    // rescale icons for tree_ctrl
    for (ScalableBitmap& bmp : m_scaled_icons_list)
        bmp.msw_rescale();
    // recreate and set new ImageList for tree_ctrl
    m_icons->RemoveAll();
    m_icons = new wxImageList(m_scaled_icons_list.front().bmp().GetWidth(), m_scaled_icons_list.front().bmp().GetHeight());
    for (ScalableBitmap& bmp : m_scaled_icons_list)
        m_icons->Add(bmp.bmp());
    m_treectrl->AssignImageList(m_icons);

    // rescale options_groups
    for (auto page : m_pages)
        page->msw_rescale();

    Layout();
}

Field* Tab::get_field(const t_config_option_key& opt_key, int opt_index/* = -1*/) const
{
	Field* field = nullptr;
	for (auto page : m_pages) {
		field = page->get_field(opt_key, opt_index);
		if (field != nullptr)
			return field;
	}
	return field;
}

// Set a key/value pair on this page. Return true if the value has been modified.
// Currently used for distributing extruders_count over preset pages of Slic3r::GUI::Tab::Printer
// after a preset is loaded.
bool Tab::set_value(const t_config_option_key& opt_key, const boost::any& value) {
	bool changed = false;
	for(auto page: m_pages) {
		if (page->set_value(opt_key, value))
		changed = true;
	}
	return changed;
}

// To be called by custom widgets, load a value into a config,
// update the preset selection boxes (the dirty flags)
// If value is saved before calling this function, put saved_value = true,
// and value can be some random value because in this case it will not been used
void Tab::load_key_value(const std::string& opt_key, const boost::any& value, bool saved_value /*= false*/)
{
	if (!saved_value) change_opt_value(*m_config, opt_key, value);
	// Mark the print & filament enabled if they are compatible with the currently selected preset.
	if (opt_key == "compatible_printers" || opt_key == "compatible_prints") {
		// Don't select another profile if this profile happens to become incompatible.
		m_preset_bundle->update_compatible(false);
	}
	m_presets->update_dirty_ui(m_presets_choice);
	on_presets_changed();
	update();
}

static wxString support_combo_value_for_config(const DynamicPrintConfig &config, bool is_fff)
{
    const std::string support         = is_fff ? "support_material"                 : "supports_enable";
    const std::string buildplate_only = is_fff ? "support_material_buildplate_only" : "support_buildplate_only";
	return
		! config.opt_bool(support) ?
        	_("None") :
			(is_fff && !config.opt_bool("support_material_auto")) ?
				_("For support enforcers only") :
                (config.opt_bool(buildplate_only) ? _("Support on build plate only") :
				                                    _("Everywhere"));
}

void Tab::on_value_change(const std::string& opt_key, const boost::any& value)
{
	if (wxGetApp().plater() == nullptr) {
		return;
	}

    const bool is_fff = supports_printer_technology(ptFFF);
	ConfigOptionsGroup* og_freq_chng_params = wxGetApp().sidebar().og_freq_chng_params(is_fff);
    if (opt_key == "fill_density" || opt_key == "pad_enable")
	{
        boost::any val = og_freq_chng_params->get_config_value(*m_config, opt_key);
        og_freq_chng_params->set_value(opt_key, val);
	}

	if (is_fff ?
			(opt_key == "support_material" || opt_key == "support_material_auto" || opt_key == "support_material_buildplate_only") :
        	(opt_key == "supports_enable"  || opt_key == "support_buildplate_only"))
		og_freq_chng_params->set_value("support", support_combo_value_for_config(*m_config, is_fff));

	if (opt_key == "brim_width")
	{
		bool val = m_config->opt_float("brim_width") > 0.0 ? true : false;
        og_freq_chng_params->set_value("brim", val);
	}

    if (opt_key == "wipe_tower" || opt_key == "single_extruder_multi_material" || opt_key == "extruders_count" )
        update_wiping_button_visibility();

    if (opt_key == "extruders_count")
        wxGetApp().plater()->on_extruders_change(boost::any_cast<size_t>(value));

	update();
}

// Show/hide the 'purging volumes' button
void Tab::update_wiping_button_visibility() {
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME
    bool wipe_tower_enabled = dynamic_cast<ConfigOptionBool*>(  (m_preset_bundle->prints.get_edited_preset().config  ).option("wipe_tower"))->value;
    bool multiple_extruders = dynamic_cast<ConfigOptionFloats*>((m_preset_bundle->printers.get_edited_preset().config).option("nozzle_diameter"))->values.size() > 1;

    auto wiping_dialog_button = wxGetApp().sidebar().get_wiping_dialog_button();
    if (wiping_dialog_button) {
        wiping_dialog_button->Show(wipe_tower_enabled && multiple_extruders);
        wiping_dialog_button->GetParent()->Layout();
    }
}


// Call a callback to update the selection of presets on the platter:
// To update the content of the selection boxes,
// to update the filament colors of the selection boxes,
// to update the "dirty" flags of the selection boxes,
// to update number of "filament" selection boxes when the number of extruders change.
void Tab::on_presets_changed()
{
	if (wxGetApp().plater() == nullptr) {
		return;
	}

    // Instead of PostEvent (EVT_TAB_PRESETS_CHANGED) just call update_presets
    wxGetApp().plater()->sidebar().update_presets(m_type);
	update_preset_description_line();

    // Printer selected at the Printer tab, update "compatible" marks at the print and filament selectors.
    for (auto t: m_dependent_tabs)
    {
        // If the printer tells us that the print or filament/sla_material preset has been switched or invalidated,
        // refresh the print or filament/sla_material tab page.
        wxGetApp().get_tab(t)->load_current_preset();
    }
    // clear m_dependent_tabs after first update from select_preset()
    // to avoid needless preset loading from update() function
    m_dependent_tabs.clear();
}

void Tab::update_preset_description_line()
{
	const Preset* parent = m_presets->get_selected_preset_parent();
	const Preset& preset = m_presets->get_edited_preset();

	wxString description_line;

	if (preset.is_default) {
		description_line = _(L("This is a default preset."));
	} else if (preset.is_system) {
		description_line = _(L("This is a system preset."));
	} else if (parent == nullptr) {
		description_line = _(L("Current preset is inherited from the default preset."));
	} else {
		description_line = wxString::Format(
			_(L("Current preset is inherited from:\n\t%s")), GUI::from_u8(parent->name));
	}

	if (preset.is_default || preset.is_system)
		description_line += "\n\t" + _(L("It can't be deleted or modified.")) + 
							"\n\t" + _(L("Any modifications should be saved as a new preset inherited from this one.")) + 
							"\n\t" + _(L("To do that please specify a new name for the preset."));
	
	if (parent && parent->vendor)
	{
		description_line += "\n\n" + _(L("Additional information:")) + "\n";
		description_line += "\t" + _(L("vendor")) + ": " + (m_type == Slic3r::Preset::TYPE_PRINTER ? "\n\t\t" : "") + parent->vendor->name +
							", ver: " + parent->vendor->config_version.to_string();
		if (m_type == Slic3r::Preset::TYPE_PRINTER) {
			const std::string &printer_model = preset.config.opt_string("printer_model");
			if (! printer_model.empty())
				description_line += "\n\n\t" + _(L("printer model")) + ": \n\t\t" + printer_model;
			switch (preset.printer_technology()) {
			case ptFFF:
			{
				//FIXME add prefered_sla_material_profile for SLA
				const std::string              &default_print_profile = preset.config.opt_string("default_print_profile");
				const std::vector<std::string> &default_filament_profiles = preset.config.option<ConfigOptionStrings>("default_filament_profile")->values;
				if (!default_print_profile.empty())
					description_line += "\n\n\t" + _(L("default print profile")) + ": \n\t\t" + default_print_profile;
				if (!default_filament_profiles.empty())
				{
					description_line += "\n\n\t" + _(L("default filament profile")) + ": \n\t\t";
					for (auto& profile : default_filament_profiles) {
						if (&profile != &*default_filament_profiles.begin())
							description_line += ", ";
						description_line += profile;
					}
				}
				break;
			}
			case ptSLA:
			{
				//FIXME add prefered_sla_material_profile for SLA
				const std::string &default_sla_material_profile = preset.config.opt_string("default_sla_material_profile");
				if (!default_sla_material_profile.empty())
					description_line += "\n\n\t" + _(L("default SLA material profile")) + ": \n\t\t" + default_sla_material_profile;

				const std::string &default_sla_print_profile = preset.config.opt_string("default_sla_print_profile");
				if (!default_sla_print_profile.empty())
					description_line += "\n\n\t" + _(L("default SLA print profile")) + ": \n\t\t" + default_sla_print_profile;
				break;
			}
			}
		}
	}

	m_parent_preset_description_line->SetText(description_line, false);
}

void Tab::update_frequently_changed_parameters()
{
	const bool is_fff = supports_printer_technology(ptFFF);
	auto og_freq_chng_params = wxGetApp().sidebar().og_freq_chng_params(is_fff);
    if (!og_freq_chng_params) return;

	og_freq_chng_params->set_value("support", support_combo_value_for_config(*m_config, is_fff));

    const std::string updated_value_key = is_fff ? "fill_density" : "pad_enable";

    const boost::any val = og_freq_chng_params->get_config_value(*m_config, updated_value_key);
    og_freq_chng_params->set_value(updated_value_key, val);

    if (is_fff)
    {
        og_freq_chng_params->set_value("brim", bool(m_config->opt_float("brim_width") > 0.0));
        update_wiping_button_visibility();
    }
}

void TabPrint::build()
{
	m_presets = &m_preset_bundle->prints;
	load_initial_data();

	auto page = add_options_page(_(L("Layers and perimeters")), "layers");
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

	page = add_options_page(_(L("Infill")), "infill");
		optgroup = page->new_optgroup(_(L("Infill")));
		optgroup->append_single_option_line("fill_density");
		optgroup->append_single_option_line("fill_pattern");
		optgroup->append_single_option_line("top_fill_pattern");
		optgroup->append_single_option_line("bottom_fill_pattern");

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

	page = add_options_page(_(L("Skirt and brim")), "skirt+brim");
		optgroup = page->new_optgroup(_(L("Skirt")));
		optgroup->append_single_option_line("skirts");
		optgroup->append_single_option_line("skirt_distance");
		optgroup->append_single_option_line("skirt_height");
		optgroup->append_single_option_line("min_skirt_length");

		optgroup = page->new_optgroup(_(L("Brim")));
		optgroup->append_single_option_line("brim_width");

	page = add_options_page(_(L("Support material")), "support");
		optgroup = page->new_optgroup(_(L("Support material")));
		optgroup->append_single_option_line("support_material");
		optgroup->append_single_option_line("support_material_auto");
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

	page = add_options_page(_(L("Speed")), "time");
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
#ifdef HAS_PRESSURE_EQUALIZER
		optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_positive");
		optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_negative");
#endif /* HAS_PRESSURE_EQUALIZER */

	page = add_options_page(_(L("Multiple Extruders")), "funnel");
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
        optgroup->append_single_option_line("single_extruder_multi_material_priming");

		optgroup = page->new_optgroup(_(L("Advanced")));
		optgroup->append_single_option_line("interface_shells");

	page = add_options_page(_(L("Advanced")), "wrench");
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

		optgroup = page->new_optgroup(_(L("Slicing")));
		optgroup->append_single_option_line("slice_closing_radius");
		optgroup->append_single_option_line("resolution");
		optgroup->append_single_option_line("xy_size_compensation");
		optgroup->append_single_option_line("elefant_foot_compensation");

		optgroup = page->new_optgroup(_(L("Other")));
		optgroup->append_single_option_line("clip_multipart_objects");

	page = add_options_page(_(L("Output options")), "output+page_white");
		optgroup = page->new_optgroup(_(L("Sequential printing")));
		optgroup->append_single_option_line("complete_objects");
		line = { _(L("Extruder clearance (mm)")), "" };
		Option option = optgroup->get_option("extruder_clearance_radius");
		option.opt.width = 6;
		line.append_option(option);
		option = optgroup->get_option("extruder_clearance_height");
		option.opt.width = 6;
		line.append_option(option);
		optgroup->append_line(line);

		optgroup = page->new_optgroup(_(L("Output file")));
		optgroup->append_single_option_line("gcode_comments");
		optgroup->append_single_option_line("gcode_label_objects");
		option = optgroup->get_option("output_filename_format");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("Post-processing scripts")), 0);	
		option = optgroup->get_option("post_process");
		option.opt.full_width = true;
        option.opt.height = 5;//50;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Notes")), "note.png");
		optgroup = page->new_optgroup(_(L("Notes")), 0);						
		option = optgroup->get_option("notes");
		option.opt.full_width = true;
        option.opt.height = 25;//250;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Dependencies")), "wrench.png");
		optgroup = page->new_optgroup(_(L("Profile dependencies")));
        line = optgroup->create_single_option_line("compatible_printers");
        line.widget = [this](wxWindow* parent) {
			return compatible_widget_create(parent, m_compatible_printers);
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
void TabPrint::reload_config()
{
	this->compatible_widget_reload(m_compatible_printers);
	Tab::reload_config();
}

void TabPrint::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME

    // #ys_FIXME_to_delete
    //! Temporary workaround for the correct updates of the TextCtrl (like "layer_height"):
    // KillFocus() for the wxSpinCtrl use CallAfter function. So,
    // to except the duplicate call of the update() after dialog->ShowModal(),
    // let check if this process is already started.
    if (is_msg_dlg_already_exist)
        return;

    m_update_cnt++;
//	Freeze();

    // layer_height shouldn't be equal to zero
    if (m_config->opt_float("layer_height") < EPSILON)
    {
        const wxString msg_text = _(L("Zero layer height is not valid.\n\nThe layer height will be reset to 0.01."));
        auto dialog = new wxMessageDialog(parent(), msg_text, _(L("Layer height")), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *m_config;
        is_msg_dlg_already_exist = true;
        dialog->ShowModal();
        new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.01));
        load_config(new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (fabs(m_config->option<ConfigOptionFloatOrPercent>("first_layer_height")->value - 0) < EPSILON)
    {
        const wxString msg_text = _(L("Zero first layer height is not valid.\n\nThe first layer height will be reset to 0.01."));
        auto dialog = new wxMessageDialog(parent(), msg_text, _(L("First layer height")), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *m_config;
        is_msg_dlg_already_exist = true;
        dialog->ShowModal();
        new_conf.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(0.01, false));
        load_config(new_conf);
        is_msg_dlg_already_exist = false;
    }

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
		if (!str_fill_pattern.empty()) {
			const std::vector<std::string> &external_fill_pattern = m_config->def()->get("top_fill_pattern")->enum_values;
			bool correct_100p_fill = false;
			for (const std::string &fill : external_fill_pattern)
			{
				if (str_fill_pattern == fill)
					correct_100p_fill = true;
			}
			// get fill_pattern name from enum_labels for using this one at dialog_msg
			str_fill_pattern = _utf8(m_config->def()->get("fill_pattern")->enum_labels[fill_pattern]);
			if (!correct_100p_fill) {
				wxString msg_text = GUI::from_u8((boost::format(_utf8(L("The %1% infill pattern is not supposed to work at 100%% density.\n\n"
					                                           "Shall I switch to rectilinear fill pattern?"))) % str_fill_pattern).str());
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
	for (auto el : {"top_fill_pattern", "bottom_fill_pattern", "infill_first", "solid_infill_extruder",
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
	bool have_support_material_auto = have_support_material && m_config->opt_bool("support_material_auto");
	bool have_support_interface = m_config->opt_int("support_material_interface_layers") > 0;
	bool have_support_soluble = have_support_material && m_config->opt_float("support_material_contact_distance") == 0;
	for (auto el : {"support_material_pattern", "support_material_with_sheath",
					"support_material_spacing", "support_material_angle", "support_material_interface_layers",
					"dont_support_bridges", "support_material_extrusion_width", "support_material_contact_distance",
					"support_material_xy_spacing" })
		get_field(el)->toggle(have_support_material);
	get_field("support_material_threshold")->toggle(have_support_material_auto);

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
    Layout();

//	Thaw();
    m_update_cnt--;

    if (m_update_cnt==0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabPrint::OnActivate()
{
	m_recommended_thin_wall_thickness_description_line->SetText(
		from_u8(PresetHints::recommended_thin_wall_thickness(*m_preset_bundle)));
	Tab::OnActivate();
}

void TabFilament::add_filament_overrides_page()
{
    PageShp page = add_options_page(_(L("Filament Overrides")), "wrench");
    ConfigOptionsGroupShp optgroup = page->new_optgroup(_(L("Retraction")));

    auto append_single_option_line = [optgroup, this](const std::string& opt_key, int opt_index)
    {
        Line line {"",""};
        if (opt_key == "filament_retract_lift_above" || opt_key == "filament_retract_lift_below") {
            Option opt = optgroup->get_option(opt_key);
            opt.opt.label = opt.opt.full_label;
            line = optgroup->create_single_option_line(opt);
        }
        else
            line = optgroup->create_single_option_line(optgroup->get_option(opt_key));

        line.near_label_widget = [this, optgroup, opt_key, opt_index](wxWindow* parent) {
            wxCheckBox* check_box = new wxCheckBox(parent, wxID_ANY, "");

            check_box->Bind(wxEVT_CHECKBOX, [this, optgroup, opt_key, opt_index](wxCommandEvent& evt) {
                const bool is_checked = evt.IsChecked();
                Field* field = optgroup->get_fieldc(opt_key, opt_index);
                if (field != nullptr) {
                    field->toggle(is_checked);
                    if (is_checked)
                        field->set_last_meaningful_value();
                    else
                        field->set_na_value();
                }
            }, check_box->GetId());

            m_overrides_options[opt_key] = check_box;
            return check_box;
        };

        optgroup->append_line(line);
    };

    const int extruder_idx = 0; // #ys_FIXME

    for (const std::string opt_key : {  "filament_retract_length",
                                        "filament_retract_lift",
                                        "filament_retract_lift_above",
                                        "filament_retract_lift_below",
                                        "filament_retract_speed",
                                        "filament_deretract_speed",
                                        "filament_retract_restart_extra",
                                        "filament_retract_before_travel",
                                        "filament_retract_layer_change",
                                        "filament_wipe",
                                        "filament_retract_before_wipe"
                                     })
        append_single_option_line(opt_key, extruder_idx);
}

void TabFilament::update_filament_overrides_page()
{
    const auto page_it = std::find_if(m_pages.begin(), m_pages.end(), [](const PageShp page) {return page->title() == _(L("Filament Overrides")); });
    if (page_it == m_pages.end())
        return;
    PageShp page = *page_it;

    const auto og_it = std::find_if(page->m_optgroups.begin(), page->m_optgroups.end(), [](const ConfigOptionsGroupShp og) {return og->title == _(L("Retraction")); });
    if (og_it == page->m_optgroups.end())
        return;
    ConfigOptionsGroupShp optgroup = *og_it;

    std::vector<std::string> opt_keys = {   "filament_retract_length", 
                                            "filament_retract_lift", 
                                            "filament_retract_lift_above", 
                                            "filament_retract_lift_below",
                                            "filament_retract_speed",
                                            "filament_deretract_speed",
                                            "filament_retract_restart_extra",
                                            "filament_retract_before_travel",
                                            "filament_retract_layer_change",
                                            "filament_wipe",
                                            "filament_retract_before_wipe"
                                        };

    const int extruder_idx = 0; // #ys_FIXME

    const bool have_retract_length = m_config->option("filament_retract_length")->is_nil() ? false : 
                                     m_config->opt_float("filament_retract_length", extruder_idx) > 0;

    for (const std::string& opt_key : opt_keys)
    {
        bool is_checked = opt_key=="filament_retract_length" ? true : have_retract_length;
        Field* field = optgroup->get_fieldc(opt_key, extruder_idx);
        if (field != nullptr) {
            is_checked &= !m_config->option(opt_key)->is_nil();
            m_overrides_options[opt_key]->SetValue(is_checked);
            field->toggle(is_checked);
        }
    }
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

		optgroup = page->new_optgroup(_(L("Temperature")) + wxString(" °C", wxConvUTF8));
		Line line = { _(L("Extruder")), "" };
		line.append_option(optgroup->get_option("first_layer_temperature"));
		line.append_option(optgroup->get_option("temperature"));
		optgroup->append_line(line);

		line = { _(L("Bed")), "" };
		line.append_option(optgroup->get_option("first_layer_bed_temperature"));
		line.append_option(optgroup->get_option("bed_temperature"));
		optgroup->append_line(line);

	page = add_options_page(_(L("Cooling")), "cooling");
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

		optgroup = page->new_optgroup(_(L("Cooling thresholds")), 25);
		optgroup->append_single_option_line("fan_below_layer_time");
		optgroup->append_single_option_line("slowdown_below_layer_time");
		optgroup->append_single_option_line("min_print_speed");

	page = add_options_page(_(L("Advanced")), "wrench");
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

        optgroup = page->new_optgroup(_(L("Wipe tower parameters")));
        optgroup->append_single_option_line("filament_minimal_purge_on_wipe_tower");

        optgroup = page->new_optgroup(_(L("Toolchange parameters with single extruder MM printers")));
		optgroup->append_single_option_line("filament_loading_speed_start");
        optgroup->append_single_option_line("filament_loading_speed");
        optgroup->append_single_option_line("filament_unloading_speed_start");
        optgroup->append_single_option_line("filament_unloading_speed");
		optgroup->append_single_option_line("filament_load_time");
		optgroup->append_single_option_line("filament_unload_time");
        optgroup->append_single_option_line("filament_toolchange_delay");
        optgroup->append_single_option_line("filament_cooling_moves");
        optgroup->append_single_option_line("filament_cooling_initial_speed");
        optgroup->append_single_option_line("filament_cooling_final_speed");

        line = optgroup->create_single_option_line("filament_ramming_parameters");// { _(L("Ramming")), "" };
        line.widget = [this](wxWindow* parent) {
			auto ramming_dialog_btn = new wxButton(parent, wxID_ANY, _(L("Ramming settings"))+dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
			ramming_dialog_btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
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


    add_filament_overrides_page();


        const int gcode_field_height = 15; // 150
        const int notes_field_height = 25; // 250

    page = add_options_page(_(L("Custom G-code")), "cog");
		optgroup = page->new_optgroup(_(L("Start G-code")), 0);
		Option option = optgroup->get_option("start_filament_gcode");
		option.opt.full_width = true;
        option.opt.height = gcode_field_height;// 150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("End G-code")), 0);
		option = optgroup->get_option("end_filament_gcode");
		option.opt.full_width = true;
		option.opt.height = gcode_field_height;// 150;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Notes")), "note.png");
		optgroup = page->new_optgroup(_(L("Notes")), 0);
		optgroup->label_width = 0;
		option = optgroup->get_option("filament_notes");
		option.opt.full_width = true;
		option.opt.height = notes_field_height;// 250;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Dependencies")), "wrench.png");
		optgroup = page->new_optgroup(_(L("Profile dependencies")));
        
        line = optgroup->create_single_option_line("compatible_printers");
        line.widget = [this](wxWindow* parent) {
			return compatible_widget_create(parent, m_compatible_printers);
		};
		optgroup->append_line(line, &m_colored_Label);
		option = optgroup->get_option("compatible_printers_condition");
		option.opt.full_width = true;
		optgroup->append_single_option_line(option);

        line = optgroup->create_single_option_line("compatible_prints");
        line.widget = [this](wxWindow* parent) {
			return compatible_widget_create(parent, m_compatible_prints);
		};
		optgroup->append_line(line, &m_colored_Label);
		option = optgroup->get_option("compatible_prints_condition");
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
void TabFilament::reload_config()
{
	this->compatible_widget_reload(m_compatible_printers);
	this->compatible_widget_reload(m_compatible_prints);
	Tab::reload_config();
}

void TabFilament::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME

    m_update_cnt++;

	wxString text = from_u8(PresetHints::cooling_description(m_presets->get_edited_preset()));
	m_cooling_description_line->SetText(text);
	text = from_u8(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle));
	m_volumetric_speed_description_line->SetText(text);
    Layout();

	bool cooling = m_config->opt_bool("cooling", 0);
	bool fan_always_on = cooling || m_config->opt_bool("fan_always_on", 0);

	for (auto el : { "max_fan_speed", "fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed" })
		get_field(el)->toggle(cooling);

	for (auto el : { "min_fan_speed", "disable_fan_first_layers" })
		get_field(el)->toggle(fan_always_on);

    update_filament_overrides_page();

    m_update_cnt--;

    if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabFilament::OnActivate()
{
	m_volumetric_speed_description_line->SetText(from_u8(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle)));
	Tab::OnActivate();
}

wxSizer* Tab::description_line_widget(wxWindow* parent, ogStaticText* *StaticText)
{
	*StaticText = new ogStaticText(parent, "");

//	auto font = (new wxSystemSettings)->GetFont(wxSYS_DEFAULT_GUI_FONT);
	(*StaticText)->SetFont(wxGetApp().normal_font());

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(*StaticText, 1, wxEXPAND|wxALL, 0);
	return sizer;
}

bool Tab::current_preset_is_dirty()
{
	return m_presets->current_is_dirty();
}

void TabPrinter::build_printhost(ConfigOptionsGroup *optgroup)
{
	const PrinterTechnology tech = m_presets->get_selected_preset().printer_technology();

	// Only offer the host type selection for FFF, for SLA it's always the SL1 printer (at the moment)
	if (tech == ptFFF) {
		optgroup->append_single_option_line("host_type");
	}

	auto printhost_browse = [=](wxWindow* parent) {
        add_scaled_button(parent, &m_printhost_browse_btn, "browse", _(L("Browse")) + " "+ dots, wxBU_LEFT | wxBU_EXACTFIT);
        ScalableButton* btn = m_printhost_browse_btn;
		btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());

		auto sizer = new wxBoxSizer(wxHORIZONTAL);
		sizer->Add(btn);

		btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent &e) {
			BonjourDialog dialog(parent, tech);
			if (dialog.show_and_lookup()) {
				optgroup->set_value("print_host", std::move(dialog.get_selected()), true);
				optgroup->get_field("print_host")->field_changed();
			}
		});

		return sizer;
	};

	auto print_host_test = [this](wxWindow* parent) {
        add_scaled_button(parent, &m_print_host_test_btn, "test", _(L("Test")), wxBU_LEFT | wxBU_EXACTFIT);
        ScalableButton* btn = m_print_host_test_btn;
        btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
		auto sizer = new wxBoxSizer(wxHORIZONTAL);
		sizer->Add(btn);

		btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
			std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
			if (! host) {
				const auto text = wxString::Format("%s",
					_(L("Could not get a valid Printer Host reference")));
				show_error(this, text);
				return;
			}
			wxString msg;
			if (host->test(msg)) {
				show_info(this, host->get_test_ok_msg(), _(L("Success!")));
			} else {
				show_error(this, host->get_test_failed_msg(msg));
			}
		});

		return sizer;
	};

	Line host_line = optgroup->create_single_option_line("print_host");
	host_line.append_widget(printhost_browse);
	host_line.append_widget(print_host_test);
	optgroup->append_line(host_line);
	optgroup->append_single_option_line("printhost_apikey");

	const auto ca_file_hint = _(L("HTTPS CA file is optional. It is only needed if you use HTTPS with a self-signed certificate."));

	if (Http::ca_file_supported()) {   
		Line cafile_line = optgroup->create_single_option_line("printhost_cafile");

		auto printhost_cafile_browse = [this, optgroup] (wxWindow* parent) {
			auto btn = new wxButton(parent, wxID_ANY, " " + _(L("Browse"))+" " +dots, wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
			btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
			btn->SetBitmap(create_scaled_bitmap(this, "browse"));
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, [this, optgroup] (wxCommandEvent e) {
				static const auto filemasks = _(L("Certificate files (*.crt, *.pem)|*.crt;*.pem|All files|*.*"));
				wxFileDialog openFileDialog(this, _(L("Open CA certificate file")), "", "", filemasks, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
				if (openFileDialog.ShowModal() != wxID_CANCEL) {
					optgroup->set_value("printhost_cafile", std::move(openFileDialog.GetPath()), true);
					optgroup->get_field("printhost_cafile")->field_changed();
				}
			});

			return sizer;
		};

		cafile_line.append_widget(printhost_cafile_browse);
		optgroup->append_line(cafile_line);

		Line cafile_hint { "", "" };
		cafile_hint.full_width = 1;
		cafile_hint.widget = [this, ca_file_hint](wxWindow* parent) {
			auto txt = new wxStaticText(parent, wxID_ANY, ca_file_hint);
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(txt);
			return sizer;
		};
		optgroup->append_line(cafile_hint);
	} else {
		Line line { "", "" };
		line.full_width = 1;

		line.widget = [this, ca_file_hint] (wxWindow* parent) {
			auto txt = new wxStaticText(parent, wxID_ANY, wxString::Format("%s\n\n\t%s",
	wxString::Format(_(L("HTTPS CA File:\n\
    \tOn this system, %s uses HTTPS certificates from the system Certificate Store or Keychain.\n\
    \tTo use a custom CA file, please import your CA file into Certificate Store / Keychain.")), SLIC3R_APP_NAME),
				ca_file_hint));
			txt->SetFont(Slic3r::GUI::wxGetApp().normal_font());
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(txt);
			return sizer;
		};

		optgroup->append_line(line);
	}
}

void TabPrinter::build()
{
	m_presets = &m_preset_bundle->printers;
	load_initial_data();

    m_printer_technology = m_presets->get_selected_preset().printer_technology();

    m_presets->get_selected_preset().printer_technology() == ptSLA ? build_sla() : build_fff();
}

void TabPrinter::build_fff()
{
    if (!m_pages.empty())
        m_pages.resize(0);
	// to avoid redundant memory allocation / deallocation during extruders count changing
	m_pages.reserve(30);

	auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"));
	m_initial_extruders_count = m_extruders_count = nozzle_diameter->values.size();
    wxGetApp().sidebar().update_objects_list_extruder_column(m_initial_extruders_count);

	const Preset* parent_preset = m_presets->get_selected_preset_parent();
	m_sys_extruders_count = parent_preset == nullptr ? 0 :
			static_cast<const ConfigOptionFloats*>(parent_preset->config.option("nozzle_diameter"))->values.size();

	auto page = add_options_page(_(L("General")), "printer");
		auto optgroup = page->new_optgroup(_(L("Size and coordinates")));

        Line line = optgroup->create_single_option_line("bed_shape");//{ _(L("Bed shape")), "" };
		line.widget = [this](wxWindow* parent) {
            ScalableButton* btn;
            add_scaled_button(parent, &btn, "printer_white",  " " + _(L("Set")) + " " + dots, wxBU_LEFT | wxBU_EXACTFIT);
            btn->SetFont(wxGetApp().normal_font());

			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e)
			{
                BedShapeDialog dlg(this);
                dlg.build_dialog(m_config->option<ConfigOptionPoints>("bed_shape"));
                if (dlg.ShowModal() == wxID_OK) {
                    std::vector<Vec2d> shape = dlg.GetValue();
                    if (!shape.empty())
                    {
                        load_key_value("bed_shape", shape);
                        update_changed_ui();
                    }
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
			def.set_default_value(new ConfigOptionInt(1)); 
			def.label = L("Extruders");
			def.tooltip = L("Number of extruders of the printer.");
			def.min = 1;
            def.mode = comExpert;
		Option option(def, "extruders_count");
		optgroup->append_single_option_line(option);
		optgroup->append_single_option_line("single_extruder_multi_material");

		optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value) {
			size_t extruders_count = boost::any_cast<int>(optgroup->get_value("extruders_count"));
			wxTheApp->CallAfter([this, opt_key, value, extruders_count]() {
				if (opt_key == "extruders_count" || opt_key == "single_extruder_multi_material") {
					extruders_count_changed(extruders_count);
                    init_options_list(); // m_options_list should be updated before UI updating
					update_dirty();
                    if (opt_key == "single_extruder_multi_material") { // the single_extruder_multimaterial was added to force pages
                        on_value_change(opt_key, value);                      // rebuild - let's make sure the on_value_change is not skipped

                        if (boost::any_cast<bool>(value) && m_extruders_count > 1) {
                            SuppressBackgroundProcessingUpdate sbpu;
                            std::vector<double> nozzle_diameters = static_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values;
                            const double frst_diam = nozzle_diameters[0];

                            for (auto cur_diam : nozzle_diameters) {
                                // if value is differs from first nozzle diameter value
                                if (fabs(cur_diam - frst_diam) > EPSILON) {
                                    const wxString msg_text = _(L("Single Extruder Multi Material is selected, \n"
                                                                  "and all extruders must have the same diameter.\n"
                                                                  "Do you want to change the diameter for all extruders to first extruder nozzle diameter value?"));
                                    auto dialog = new wxMessageDialog(parent(), msg_text, _(L("Nozzle diameter")), wxICON_WARNING | wxYES_NO);
                                    
                                    if (dialog->ShowModal() == wxID_YES) {
                                        DynamicPrintConfig new_conf = *m_config;
                                        for (size_t i = 1; i < nozzle_diameters.size(); i++)
                                            nozzle_diameters[i] = frst_diam;

                                        new_conf.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzle_diameters));
                                        load_config(new_conf);
                                    }
                                    break;
                                }
                            } 
                        }
                    }
				}
				else {
					update_dirty();
					on_value_change(opt_key, value);
				}
			});
		};


#if 0
		if (!m_no_controller)
		{
		optgroup = page->new_optgroup(_(L("USB/Serial connection")));
			line = {_(L("Serial port")), ""};
			Option serial_port = optgroup->get_option("serial_port");
			serial_port.side_widget = ([this](wxWindow* parent) {
				auto btn = new wxBitmapButton(parent, wxID_ANY, wxBitmap(from_u8(Slic3r::var("arrow_rotate_clockwise.png")), wxBITMAP_TYPE_PNG),
					wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
				btn->SetToolTip(_(L("Rescan serial ports")));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent e) {update_serial_ports(); });
				return sizer;
			});
			auto serial_test = [this](wxWindow* parent) {
				auto btn = m_serial_test_btn = new wxButton(parent, wxID_ANY,
					_(L("Test")), wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
				btn->SetFont(Slic3r::GUI::small_font());
				btn->SetBitmap(wxBitmap(from_u8(Slic3r::var("wrench.png")), wxBITMAP_TYPE_PNG));
				auto sizer = new wxBoxSizer(wxHORIZONTAL);
				sizer->Add(btn);

				btn->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent e) {
					auto sender = Slic3r::make_unique<GCodeSender>();
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
#endif

		optgroup = page->new_optgroup(_(L("Print Host upload")));
		build_printhost(optgroup.get());

		optgroup = page->new_optgroup(_(L("Firmware")));
		optgroup->append_single_option_line("gcode_flavor");
		optgroup->append_single_option_line("silent_mode");
		optgroup->append_single_option_line("remaining_times");

		optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value) {
			wxTheApp->CallAfter([this, opt_key, value]() {
				if (opt_key == "silent_mode") {
					bool val = boost::any_cast<bool>(value);
					if (m_use_silent_mode != val) {
						m_rebuild_kinematics_page = true;
						m_use_silent_mode = val;
					}
				}
				build_unregular_pages();
				update_dirty();
				on_value_change(opt_key, value);
			});
		};

		optgroup = page->new_optgroup(_(L("Advanced")));
		optgroup->append_single_option_line("use_relative_e_distances");
		optgroup->append_single_option_line("use_firmware_retraction");
		optgroup->append_single_option_line("use_volumetric_e");
		optgroup->append_single_option_line("variable_layer_height");

    const int gcode_field_height = 15; // 150
    const int notes_field_height = 25; // 250
	page = add_options_page(_(L("Custom G-code")), "cog");
		optgroup = page->new_optgroup(_(L("Start G-code")), 0);
		option = optgroup->get_option("start_gcode");
		option.opt.full_width = true;
        option.opt.height = gcode_field_height;//150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("End G-code")), 0);
		option = optgroup->get_option("end_gcode");
		option.opt.full_width = true;
        option.opt.height = gcode_field_height;//150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("Before layer change G-code")), 0);
		option = optgroup->get_option("before_layer_gcode");
		option.opt.full_width = true;
        option.opt.height = gcode_field_height;//150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("After layer change G-code")), 0);
		option = optgroup->get_option("layer_gcode");
		option.opt.full_width = true;
        option.opt.height = gcode_field_height;//150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("Tool change G-code")), 0);
		option = optgroup->get_option("toolchange_gcode");
		option.opt.full_width = true;
        option.opt.height = gcode_field_height;//150;
		optgroup->append_single_option_line(option);

		optgroup = page->new_optgroup(_(L("Between objects G-code (for sequential printing)")), 0);
		option = optgroup->get_option("between_objects_gcode");
		option.opt.full_width = true;
        option.opt.height = gcode_field_height;//150;
		optgroup->append_single_option_line(option);
	
	page = add_options_page(_(L("Notes")), "note.png");
		optgroup = page->new_optgroup(_(L("Notes")), 0);
		option = optgroup->get_option("printer_notes");
		option.opt.full_width = true;
        option.opt.height = notes_field_height;//250;
		optgroup->append_single_option_line(option);

	page = add_options_page(_(L("Dependencies")), "wrench.png");
		optgroup = page->new_optgroup(_(L("Profile dependencies")));
		line = Line{ "", "" };
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			return description_line_widget(parent, &m_parent_preset_description_line);
		};
		optgroup->append_line(line);

	build_unregular_pages();

#if 0
	if (!m_no_controller)
		update_serial_ports();
#endif
}

void TabPrinter::build_sla()
{
    if (!m_pages.empty())
        m_pages.resize(0);
    auto page = add_options_page(_(L("General")), "printer");
    auto optgroup = page->new_optgroup(_(L("Size and coordinates")));

    Line line = optgroup->create_single_option_line("bed_shape");//{ _(L("Bed shape")), "" };
    line.widget = [this](wxWindow* parent) {
        ScalableButton* btn;
        add_scaled_button(parent, &btn, "printer_white", " " + _(L("Set")) + " " + dots, wxBU_LEFT | wxBU_EXACTFIT);
        btn->SetFont(wxGetApp().normal_font());


        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(btn);

        btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e)
        {
            BedShapeDialog dlg(this);
            dlg.build_dialog(m_config->option<ConfigOptionPoints>("bed_shape"));
            if (dlg.ShowModal() == wxID_OK) {
                std::vector<Vec2d> shape = dlg.GetValue();
                if (!shape.empty())
                {
                    load_key_value("bed_shape", shape);
                    update_changed_ui();
                }
            }
        }));

        return sizer;
    };
    optgroup->append_line(line, &m_colored_Label);
    optgroup->append_single_option_line("max_print_height");

    optgroup = page->new_optgroup(_(L("Display")));
    optgroup->append_single_option_line("display_width");
    optgroup->append_single_option_line("display_height");

    auto option = optgroup->get_option("display_pixels_x");
    line = { _(option.opt.full_label), "" };
    line.append_option(option);
    line.append_option(optgroup->get_option("display_pixels_y"));
    optgroup->append_line(line);
    optgroup->append_single_option_line("display_orientation");
    
    // FIXME: This should be on one line in the UI
    optgroup->append_single_option_line("display_mirror_x");
    optgroup->append_single_option_line("display_mirror_y");

    optgroup = page->new_optgroup(_(L("Tilt")));
    line = { _(L("Tilt time")), "" };
    line.append_option(optgroup->get_option("fast_tilt_time"));
    line.append_option(optgroup->get_option("slow_tilt_time"));
    optgroup->append_line(line);
    optgroup->append_single_option_line("area_fill");

    optgroup = page->new_optgroup(_(L("Corrections")));
    line = Line{ _(m_config->def()->get("relative_correction")->full_label), "" };
//    std::vector<std::string> axes{ "X", "Y", "Z" };
    std::vector<std::string> axes{ "XY", "Z" };
    int id = 0;
    for (auto& axis : axes) {
        auto opt = optgroup->get_option("relative_correction", id);
        opt.opt.label = axis;
        line.append_option(opt);
        ++id;
    }
    optgroup->append_line(line);
    optgroup->append_single_option_line("absolute_correction");
    optgroup->append_single_option_line("gamma_correction");

    optgroup = page->new_optgroup(_(L("Print Host upload")));
    build_printhost(optgroup.get());

    const int notes_field_height = 25; // 250

    page = add_options_page(_(L("Notes")), "note.png");
    optgroup = page->new_optgroup(_(L("Notes")), 0);
    option = optgroup->get_option("printer_notes");
    option.opt.full_width = true;
    option.opt.height = notes_field_height;//250;
    optgroup->append_single_option_line(option);

    page = add_options_page(_(L("Dependencies")), "wrench.png");
    optgroup = page->new_optgroup(_(L("Profile dependencies")));
    line = Line{ "", "" };
    line.full_width = 1;
    line.widget = [this](wxWindow* parent) {
        return description_line_widget(parent, &m_parent_preset_description_line);
    };
    optgroup->append_line(line);
}

void TabPrinter::update_serial_ports()
{
	Field *field = get_field("serial_port");
	Choice *choice = static_cast<Choice *>(field);
	choice->set_values(Utils::scan_serial_ports());
}

void TabPrinter::extruders_count_changed(size_t extruders_count)
{
    bool is_count_changed = false;
    if (m_extruders_count != extruders_count) {
	    m_extruders_count = extruders_count;
	    m_preset_bundle->printers.get_edited_preset().set_num_extruders(extruders_count);
	    m_preset_bundle->update_multi_material_filament_presets();
        is_count_changed = true;
    }

    /* This function should be call in any case because of correct updating/rebuilding 
     * of unregular pages of a Printer Settings
     */
	build_unregular_pages();

    if (is_count_changed) {
        on_value_change("extruders_count", extruders_count);
        wxGetApp().sidebar().update_objects_list_extruder_column(extruders_count);
    }
}

void TabPrinter::append_option_line(ConfigOptionsGroupShp optgroup, const std::string opt_key)
{
	auto option = optgroup->get_option(opt_key, 0);
	auto line = Line{ _(option.opt.full_label), "" };
	line.append_option(option);
	if (m_use_silent_mode)
		line.append_option(optgroup->get_option(opt_key, 1));
	optgroup->append_line(line);
}

PageShp TabPrinter::build_kinematics_page()
{
	auto page = add_options_page(_(L("Machine limits")), "cog", true);

	if (m_use_silent_mode)	{
		// Legend for OptionsGroups
		auto optgroup = page->new_optgroup("");
		optgroup->set_show_modified_btns_val(false);
        optgroup->label_width = 23;// 230;
		auto line = Line{ "", "" };

		ConfigOptionDef def;
		def.type = coString;
		def.width = 15;
		def.gui_type = "legend";
        def.mode = comAdvanced;
		def.tooltip = L("Values in this column are for Normal mode");
		def.set_default_value(new ConfigOptionString{ _(L("Normal")).ToUTF8().data() });

		auto option = Option(def, "full_power_legend");
		line.append_option(option);

		def.tooltip = L("Values in this column are for Stealth mode");
		def.set_default_value(new ConfigOptionString{ _(L("Stealth")).ToUTF8().data() });
		option = Option(def, "silent_legend");
		line.append_option(option);

		optgroup->append_line(line);
	}

	std::vector<std::string> axes{ "x", "y", "z", "e" };
	auto optgroup = page->new_optgroup(_(L("Maximum feedrates")));
		for (const std::string &axis : axes)	{
			append_option_line(optgroup, "machine_max_feedrate_" + axis);
		}

	optgroup = page->new_optgroup(_(L("Maximum accelerations")));
		for (const std::string &axis : axes)	{
			append_option_line(optgroup, "machine_max_acceleration_" + axis);
		}
		append_option_line(optgroup, "machine_max_acceleration_extruding");
		append_option_line(optgroup, "machine_max_acceleration_retracting");

	optgroup = page->new_optgroup(_(L("Jerk limits")));
		for (const std::string &axis : axes)	{
			append_option_line(optgroup, "machine_max_jerk_" + axis);
		}

	optgroup = page->new_optgroup(_(L("Minimum feedrates")));
		append_option_line(optgroup, "machine_min_extruding_rate");
		append_option_line(optgroup, "machine_min_travel_rate");

	return page;
}

/* Previous name build_extruder_pages().
 * 
 * This function was renamed because of now it implements not just an extruder pages building, 
 * but "Machine limits" and "Single extruder MM setup" too 
 * (These pages can changes according to the another values of a current preset)
 * */
void TabPrinter::build_unregular_pages()
{
	size_t		n_before_extruders = 2;			//	Count of pages before Extruder pages
	bool		is_marlin_flavor = m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value == gcfMarlin;

    /* ! Freeze/Thaw in this function is needed to avoid call OnPaint() for erased pages 
     * and be cause of application crash, when try to change Preset in moment,
     * when one of unregular pages is selected.
     *  */
    Freeze();

#ifdef __WXMSW__
    /* Workaround for correct layout of controls inside the created page:
     * In some _strange_ way we should we should imitate page resizing.
     */
    auto layout_page = [this](PageShp page)
    {
        const wxSize& sz = page->GetSize();
        page->SetSize(sz.x + 1, sz.y + 1);
        page->SetSize(sz);
    };
#endif //__WXMSW__

	// Add/delete Kinematics page according to is_marlin_flavor
	size_t existed_page = 0;
	for (int i = n_before_extruders; i < m_pages.size(); ++i) // first make sure it's not there already
		if (m_pages[i]->title().find(_(L("Machine limits"))) != std::string::npos) {
			if (!is_marlin_flavor || m_rebuild_kinematics_page)
				m_pages.erase(m_pages.begin() + i);
			else
				existed_page = i;
			break;
		}

	if (existed_page < n_before_extruders && is_marlin_flavor) {
		auto page = build_kinematics_page();
#ifdef __WXMSW__
		layout_page(page);
#endif
		m_pages.insert(m_pages.begin() + n_before_extruders, page);
	}

	if (is_marlin_flavor) 
		n_before_extruders++;
	size_t		n_after_single_extruder_MM = 2; //	Count of pages after single_extruder_multi_material page

	if (m_extruders_count_old == m_extruders_count || 
		(m_has_single_extruder_MM_page && m_extruders_count == 1))
	{
		// if we have a single extruder MM setup, add a page with configuration options:
		for (int i = 0; i < m_pages.size(); ++i) // first make sure it's not there already
			if (m_pages[i]->title().find(_(L("Single extruder MM setup"))) != std::string::npos) {
				m_pages.erase(m_pages.begin() + i);
				break;
			}
		m_has_single_extruder_MM_page = false;
	}
	if (m_extruders_count > 1 && m_config->opt_bool("single_extruder_multi_material") && !m_has_single_extruder_MM_page) {
		// create a page, but pretend it's an extruder page, so we can add it to m_pages ourselves
		auto page = add_options_page(_(L("Single extruder MM setup")), "printer", true);
		auto optgroup = page->new_optgroup(_(L("Single extruder multimaterial parameters")));
		optgroup->append_single_option_line("cooling_tube_retraction");
		optgroup->append_single_option_line("cooling_tube_length");
		optgroup->append_single_option_line("parking_pos_retraction");
        optgroup->append_single_option_line("extra_loading_move");
        optgroup->append_single_option_line("high_current_on_filament_swap");
		m_pages.insert(m_pages.end() - n_after_single_extruder_MM, page);
		m_has_single_extruder_MM_page = true;
	}
	
    // Build missed extruder pages
	for (auto extruder_idx = m_extruders_count_old; extruder_idx < m_extruders_count; ++extruder_idx) {
		//# build page
        const wxString& page_name = wxString::Format(_(L("Extruder %d")), int(extruder_idx + 1));
        auto page = add_options_page(page_name, "funnel", true);
		m_pages.insert(m_pages.begin() + n_before_extruders + extruder_idx, page);
			
			auto optgroup = page->new_optgroup(_(L("Size")));
			optgroup->append_single_option_line("nozzle_diameter", extruder_idx);

            optgroup->m_on_change = [this, extruder_idx](const t_config_option_key& opt_key, boost::any value)
            {
                if (m_config->opt_bool("single_extruder_multi_material") && m_extruders_count > 1 && opt_key.find_first_of("nozzle_diameter") != std::string::npos)
                {
                    SuppressBackgroundProcessingUpdate sbpu;
                    const double new_nd = boost::any_cast<double>(value);
                    std::vector<double> nozzle_diameters = static_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values;

                    // if value was changed
                    if (fabs(nozzle_diameters[extruder_idx == 0 ? 1 : 0] - new_nd) > EPSILON) 
                    {
                        const wxString msg_text = _(L("Do you want to change the diameter for all extruders?"));
                        auto dialog = new wxMessageDialog(parent(), msg_text, _(L("Nozzle diameter")), wxICON_WARNING | wxYES_NO);

                        DynamicPrintConfig new_conf = *m_config;
                        if (dialog->ShowModal() == wxID_YES) {
                            for (size_t i = 0; i < nozzle_diameters.size(); i++) {
                                if (i==extruder_idx)
                                    continue;
                                nozzle_diameters[i] = new_nd;
                            }
                        }
                        else 
                            nozzle_diameters[extruder_idx] = nozzle_diameters[extruder_idx == 0 ? 1 : 0];

                        new_conf.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzle_diameters));
                        load_config(new_conf);
                    }
                }

                update_dirty();
                update();
            };
		
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

#ifdef __WXMSW__
		layout_page(page);
#endif
	}
 
	// # remove extra pages
	if (m_extruders_count < m_extruders_count_old)
		m_pages.erase(	m_pages.begin() + n_before_extruders + m_extruders_count, 
						m_pages.begin() + n_before_extruders + m_extruders_count_old);

    Thaw();

	m_extruders_count_old = m_extruders_count;
	rebuild_page_tree();

    // Reload preset pages with current configuration values
    reload_config();
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

void TabPrinter::update_pages()
{
    // update m_pages ONLY if printer technology is changed
    const PrinterTechnology new_printer_technology = m_presets->get_edited_preset().printer_technology();
    if (new_printer_technology == m_printer_technology)
        return;

    // hide all old pages
    for (auto& el : m_pages)
        el.get()->Hide();

    // set m_pages to m_pages_(technology before changing)
    m_printer_technology == ptFFF ? m_pages.swap(m_pages_fff) : m_pages.swap(m_pages_sla);

    // build Tab according to the technology, if it's not exist jet OR
    // set m_pages_(technology after changing) to m_pages
    // m_printer_technology will be set by Tab::load_current_preset()
    if (new_printer_technology == ptFFF)
    {
        if (m_pages_fff.empty())
        {
            build_fff();
            if (m_extruders_count > 1)
            {
                m_preset_bundle->update_multi_material_filament_presets();
                on_value_change("extruders_count", m_extruders_count);
            }
        }
        else
            m_pages.swap(m_pages_fff);

         wxGetApp().sidebar().update_objects_list_extruder_column(m_extruders_count);
    }
    else 
        m_pages_sla.empty() ? build_sla() : m_pages.swap(m_pages_sla);

    rebuild_page_tree();
}

void TabPrinter::update()
{
    m_update_cnt++;
    m_presets->get_edited_preset().printer_technology() == ptFFF ? update_fff() : update_sla();
    m_update_cnt--;

    if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabPrinter::update_fff()
{
//	Freeze();

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

	{
		std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
		m_print_host_test_btn->Enable(!m_config->opt_string("print_host").empty() && host->can_test());
		m_printhost_browse_btn->Enable(host->has_auto_discovery());
	}

	bool have_multiple_extruders = m_extruders_count > 1;
	get_field("toolchange_gcode")->toggle(have_multiple_extruders);
	get_field("single_extruder_multi_material")->toggle(have_multiple_extruders);

	bool is_marlin_flavor = m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value == gcfMarlin;

	{
		Field *sm = get_field("silent_mode");
		if (! is_marlin_flavor)
			// Disable silent mode for non-marlin firmwares.
			get_field("silent_mode")->toggle(false);
		if (is_marlin_flavor)
			sm->enable();
		else
			sm->disable();
	}

	if (m_use_silent_mode != m_config->opt_bool("silent_mode"))	{
		m_rebuild_kinematics_page = true;
		m_use_silent_mode = m_config->opt_bool("silent_mode");
	}

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

//	Thaw();
}

void TabPrinter::update_sla()
{ ; }

// Initialize the UI from the current preset
void Tab::load_current_preset()
{
	const Preset& preset = m_presets->get_edited_preset();

	(preset.is_default || preset.is_system) ? m_btn_delete_preset->Disable() : m_btn_delete_preset->Enable(true);

    update();
	if (m_type == Slic3r::Preset::TYPE_PRINTER) {
		// For the printer profile, generate the extruder pages.
		if (preset.printer_technology() == ptFFF)
			on_preset_loaded();
		else
			wxGetApp().sidebar().update_objects_list_extruder_column(1);
	}
    // Reload preset pages with the new configuration values.
    reload_config();

    const Preset* selected_preset_parent = m_presets->get_selected_preset_parent();
    m_is_default_preset = selected_preset_parent != nullptr && selected_preset_parent->is_default;

	m_bmp_non_system = selected_preset_parent ? &m_bmp_value_unlock : &m_bmp_white_bullet;
	m_ttg_non_system = selected_preset_parent ? &m_ttg_value_unlock : &m_ttg_white_bullet_ns;
	m_tt_non_system  = selected_preset_parent ? &m_tt_value_unlock  : &m_ttg_white_bullet_ns;

//	m_undo_to_sys_btn->Enable(!preset.is_default);

#if 0
	// use CallAfter because some field triggers schedule on_change calls using CallAfter,
	// and we don't want them to be called after this update_dirty() as they would mark the 
	// preset dirty again
	// (not sure this is true anymore now that update_dirty is idempotent)
	wxTheApp->CallAfter([this]
#endif
	{
		// checking out if this Tab exists till this moment
		if (!wxGetApp().checked_tab(this))
			return;
		update_tab_ui();

        // update show/hide tabs
		if (m_type == Slic3r::Preset::TYPE_PRINTER) {
            const PrinterTechnology printer_technology = m_presets->get_edited_preset().printer_technology();
            if (printer_technology != static_cast<TabPrinter*>(this)->m_printer_technology)
            {
                for (auto tab : wxGetApp().tabs_list) {
                    if (tab->type() == Preset::TYPE_PRINTER) // Printer tab is shown every time
                        continue;
                    if (tab->supports_printer_technology(printer_technology))
                    {
                        wxGetApp().tab_panel()->InsertPage(wxGetApp().tab_panel()->FindPage(this), tab, tab->title());
                        #ifdef __linux__ // the tabs apparently need to be explicitly shown on Linux (pull request #1563)
                            int page_id = wxGetApp().tab_panel()->FindPage(tab);
                            wxGetApp().tab_panel()->GetPage(page_id)->Show(true);
                        #endif // __linux__
                    }
                    else {
                        int page_id = wxGetApp().tab_panel()->FindPage(tab);
                        wxGetApp().tab_panel()->GetPage(page_id)->Show(false);
                        wxGetApp().tab_panel()->RemovePage(page_id);
                    } 
                }
                static_cast<TabPrinter*>(this)->m_printer_technology = printer_technology;
            }
			on_presets_changed();
			if (printer_technology == ptFFF) {
				static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<TabPrinter*>(this)->m_extruders_count;
				const Preset* parent_preset = m_presets->get_selected_preset_parent();
				static_cast<TabPrinter*>(this)->m_sys_extruders_count = parent_preset == nullptr ? 0 :
					static_cast<const ConfigOptionFloats*>(parent_preset->config.option("nozzle_diameter"))->values.size();
			}
		}
		else {
			on_presets_changed();
            if (m_type == Preset::TYPE_SLA_PRINT || m_type == Preset::TYPE_PRINT)
				update_frequently_changed_parameters();
		}

		m_opt_status_value = (m_presets->get_selected_preset_parent() ? osSystemValue : 0) | osInitValue;
		init_options_list();
        update_visibility();
		update_changed_ui();
	}
#if 0
	);
#endif
}

//Regerenerate content of the page tree.
void Tab::rebuild_page_tree()
{
	// get label of the currently selected item
    const auto sel_item = m_treectrl->GetSelection();
	const auto selected = sel_item ? m_treectrl->GetItemText(sel_item) : "";
	const auto rootItem = m_treectrl->GetRootItem();

	auto have_selection = 0;
	m_treectrl->DeleteChildren(rootItem);
	for (auto p : m_pages)
	{
		auto itemId = m_treectrl->AppendItem(rootItem, p->title(), p->iconID());
		m_treectrl->SetItemTextColour(itemId, p->get_item_colour());
		if (p->title() == selected) {
			m_treectrl->SelectItem(itemId);
			have_selection = 1;
		}
	}

	if (!have_selection) {
		// this is triggered on first load, so we don't disable the sel change event
		auto item = m_treectrl->GetFirstVisibleItem();
		if (item) {
			m_treectrl->SelectItem(item);
		}
	}
}

void Tab::update_page_tree_visibility()
{
    const auto sel_item = m_treectrl->GetSelection();
    const auto selected = sel_item ? m_treectrl->GetItemText(sel_item) : "";
    const auto rootItem = m_treectrl->GetRootItem();

    auto have_selection = 0;
    m_treectrl->DeleteChildren(rootItem);
    for (auto p : m_pages)
    {
        if (!p->get_show())
            continue;
        auto itemId = m_treectrl->AppendItem(rootItem, p->title(), p->iconID());
        m_treectrl->SetItemTextColour(itemId, p->get_item_colour());
        if (p->title() == selected) {
            m_treectrl->SelectItem(itemId);
            have_selection = 1;
        }
    }

    if (!have_selection) {
        // this is triggered on first load, so we don't disable the sel change event
        auto item = m_treectrl->GetFirstVisibleItem();
        if (item) {
            m_treectrl->SelectItem(item);
        }
    }

}

// Called by the UI combo box when the user switches profiles, and also to delete the current profile.
// Select a preset by a name.If !defined(name), then the default preset is selected.
// If the current profile is modified, user is asked to save the changes.
void Tab::select_preset(std::string preset_name, bool delete_current)
{
	if (preset_name.empty()) {
		if (delete_current) {
			// Find an alternate preset to be selected after the current preset is deleted.
			const std::deque<Preset> &presets 		= this->m_presets->get_presets();
			size_t    				  idx_current   = this->m_presets->get_idx_selected();
    		// Find the next visible preset.
    		size_t 				      idx_new       = idx_current + 1;
    		if (idx_new < presets.size())
        		for (; idx_new < presets.size() && ! presets[idx_new].is_visible; ++ idx_new) ;
    		if (idx_new == presets.size())
				for (idx_new = idx_current - 1; idx_new > 0 && ! presets[idx_new].is_visible; -- idx_new);
			preset_name = presets[idx_new].name;
		} else {
			// If no name is provided, select the "-- default --" preset.
			preset_name = m_presets->default_preset().name;
		}
	}
	assert(! delete_current || (m_presets->get_edited_preset().name != preset_name && m_presets->get_edited_preset().is_user()));
	bool current_dirty = ! delete_current && m_presets->current_is_dirty();
	bool print_tab     = m_presets->type() == Preset::TYPE_PRINT || m_presets->type() == Preset::TYPE_SLA_PRINT;
	bool printer_tab   = m_presets->type() == Preset::TYPE_PRINTER;
	bool canceled      = false;
	m_dependent_tabs = {};
	if (current_dirty && ! may_discard_current_dirty_preset()) {
		canceled = true;
	} else if (print_tab) {
		// Before switching the print profile to a new one, verify, whether the currently active filament or SLA material
		// are compatible with the new print.
		// If it is not compatible and the current filament or SLA material are dirty, let user decide
		// whether to discard the changes or keep the current print selection.
		PrinterTechnology  printer_technology = m_preset_bundle->printers.get_edited_preset().printer_technology();
		PresetCollection  &dependent = (printer_technology == ptFFF) ? m_preset_bundle->filaments : m_preset_bundle->sla_materials;
        bool 			   old_preset_dirty = dependent.current_is_dirty();
        bool 			   new_preset_compatible = dependent.get_edited_preset().is_compatible_with_print(*m_presets->find_preset(preset_name, true));
        if (! canceled)
            canceled = old_preset_dirty && ! new_preset_compatible && ! may_discard_current_dirty_preset(&dependent, preset_name);
        if (! canceled) {
            // The preset will be switched to a different, compatible preset, or the '-- default --'.
            m_dependent_tabs.emplace_back((printer_technology == ptFFF) ? Preset::Type::TYPE_FILAMENT : Preset::Type::TYPE_SLA_MATERIAL);
            if (old_preset_dirty)
                dependent.discard_current_changes();
        }
	} else if (printer_tab) {
		// Before switching the printer to a new one, verify, whether the currently active print and filament
		// are compatible with the new printer.
		// If they are not compatible and the current print or filament are dirty, let user decide
		// whether to discard the changes or keep the current printer selection.
		//
		// With the introduction of the SLA printer types, we need to support switching between
		// the FFF and SLA printers.
		const Preset 		&new_printer_preset     = *m_presets->find_preset(preset_name, true);
		PrinterTechnology    old_printer_technology = m_presets->get_edited_preset().printer_technology();
		PrinterTechnology    new_printer_technology = new_printer_preset.printer_technology();
        if (new_printer_technology == ptSLA && old_printer_technology == ptFFF && !may_switch_to_SLA_preset())
            canceled = true;
        else {
            struct PresetUpdate {
                Preset::Type         tab_type;
                PresetCollection 	*presets;
                PrinterTechnology    technology;
                bool    	         old_preset_dirty;
                bool         	     new_preset_compatible;
            };
            std::vector<PresetUpdate> updates = {
                { Preset::Type::TYPE_PRINT,         &m_preset_bundle->prints,       ptFFF },
                { Preset::Type::TYPE_SLA_PRINT,     &m_preset_bundle->sla_prints,   ptSLA },
                { Preset::Type::TYPE_FILAMENT,      &m_preset_bundle->filaments,    ptFFF },
                { Preset::Type::TYPE_SLA_MATERIAL,  &m_preset_bundle->sla_materials,ptSLA }
            };
            for (PresetUpdate &pu : updates) {
                pu.old_preset_dirty = (old_printer_technology == pu.technology) && pu.presets->current_is_dirty();
                pu.new_preset_compatible = (new_printer_technology == pu.technology) && pu.presets->get_edited_preset().is_compatible_with_printer(new_printer_preset);
                if (!canceled)
                    canceled = pu.old_preset_dirty && !pu.new_preset_compatible && !may_discard_current_dirty_preset(pu.presets, preset_name);
            }
            if (!canceled) {
                for (PresetUpdate &pu : updates) {
                    // The preset will be switched to a different, compatible preset, or the '-- default --'.
                    if (pu.technology == new_printer_technology)
                        m_dependent_tabs.emplace_back(pu.tab_type);
                    if (pu.old_preset_dirty && !pu.new_preset_compatible)
                        pu.presets->discard_current_changes();
                }
            }
        }
	}

	if (! canceled && delete_current) {
		// Delete the file and select some other reasonable preset.
		// It does not matter which preset will be made active as the preset will be re-selected from the preset_name variable.
		// The 'external' presets will only be removed from the preset list, their files will not be deleted.
		try { 
			m_presets->delete_current_preset();
		} catch (const std::exception & /* e */) {
			//FIXME add some error reporting!
			canceled = true;
		}
	}

	if (canceled) {
		update_tab_ui();
		// Trigger the on_presets_changed event so that we also restore the previous value in the plater selector,
		// if this action was initiated from the platter.
		on_presets_changed();
	} else {
		if (current_dirty)
			m_presets->discard_current_changes();

		const bool is_selected = m_presets->select_preset_by_name(preset_name, false) || delete_current;
		assert(m_presets->get_edited_preset().name == preset_name || ! is_selected);
		// Mark the print & filament enabled if they are compatible with the currently selected preset.
		// The following method should not discard changes of current print or filament presets on change of a printer profile,
		// if they are compatible with the current printer.
		if (current_dirty || delete_current || print_tab || printer_tab)
			m_preset_bundle->update_compatible(true);
		// Initialize the UI from the current preset.
        if (printer_tab)
            static_cast<TabPrinter*>(this)->update_pages();

        if (! is_selected && printer_tab)
        {
            /* There is a case, when :
             * after Config Wizard applying we try to select previously selected preset, but 
             * in a current configuration this one:
             *  1. doesn't exist now,
             *  2. have another printer_technology
             * So, it is necessary to update list of dependent tabs 
             * to the corresponding printer_technology
             */
            const PrinterTechnology printer_technology = m_presets->get_edited_preset().printer_technology();
            if (printer_technology == ptFFF && m_dependent_tabs.front() != Preset::Type::TYPE_PRINT)
            	m_dependent_tabs = { Preset::Type::TYPE_PRINT, Preset::Type::TYPE_FILAMENT };
            else if (printer_technology == ptSLA && m_dependent_tabs.front() != Preset::Type::TYPE_SLA_PRINT)
                m_dependent_tabs = { Preset::Type::TYPE_SLA_PRINT, Preset::Type::TYPE_SLA_MATERIAL };
        }
		load_current_preset();
	}
}

// If the current preset is dirty, the user is asked whether the changes may be discarded.
// if the current preset was not dirty, or the user agreed to discard the changes, 1 is returned.
bool Tab::may_discard_current_dirty_preset(PresetCollection* presets /*= nullptr*/, const std::string& new_printer_name /*= ""*/)
{
	if (presets == nullptr) presets = m_presets;
	// Display a dialog showing the dirty options in a human readable form.
	const Preset& old_preset = presets->get_edited_preset();
	std::string   type_name  = presets->name();
	wxString      tab        = "          ";
	wxString      name       = old_preset.is_default ?
		wxString::Format(_(L("Default preset (%s)")), _(type_name)) :
		wxString::Format(_(L("Preset (%s)")), _(type_name)) + "\n" + tab + old_preset.name;

	// Collect descriptions of the dirty options.
	wxString changes;
	for (const std::string &opt_key : presets->current_dirty_options()) {
		const ConfigOptionDef &opt = m_config->def()->options.at(opt_key);
		/*std::string*/wxString name = "";
		if (! opt.category.empty())
			name += _(opt.category) + " > ";
		name += !opt.full_label.empty() ?
				_(opt.full_label) : 
				_(opt.label);
		changes += tab + /*from_u8*/(name) + "\n";
	}
	// Show a confirmation dialog with the list of dirty options.
	wxString message = name + "\n\n";
	if (new_printer_name.empty())
		message += _(L("has the following unsaved changes:"));
	else {
		message += (m_type == Slic3r::Preset::TYPE_PRINTER) ?
				_(L("is not compatible with printer")) :
				_(L("is not compatible with print profile"));
		message += wxString("\n") + tab + from_u8(new_printer_name) + "\n\n";
		message += _(L("and it has the following unsaved changes:"));
	}
	auto confirm = new wxMessageDialog(parent(),
		message + "\n" + changes + "\n\n" + _(L("Discard changes and continue anyway?")),
		_(L("Unsaved Changes")), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
	return confirm->ShowModal() == wxID_YES;
}

// If we are switching from the FFF-preset to the SLA, we should to control the printed objects if they have a part(s).
// Because of we can't to print the multi-part objects with SLA technology.
bool Tab::may_switch_to_SLA_preset()
{
    if (model_has_multi_part_objects(wxGetApp().model()))
    {
        show_info( parent(), 
                    _(L("It's impossible to print multi-part object(s) with SLA technology.")) + "\n\n" +
                    _(L("Please check your object list before preset changing.")),
                    _(L("Attention!")) );
        return false;
    }
    return true;
}

void Tab::OnTreeSelChange(wxTreeEvent& event)
{
	if (m_disable_tree_sel_changed_event)         
        return;

// There is a bug related to Ubuntu overlay scrollbars, see https://github.com/prusa3d/PrusaSlicer/issues/898 and https://github.com/prusa3d/PrusaSlicer/issues/952.
// The issue apparently manifests when Show()ing a window with overlay scrollbars while the UI is frozen. For this reason,
// we will Thaw the UI prematurely on Linux. This means destroing the no_updates object prematurely.
#ifdef __linux__	
	std::unique_ptr<wxWindowUpdateLocker> no_updates(new wxWindowUpdateLocker(this));
#else
//	wxWindowUpdateLocker noUpdates(this);
#endif

    if (m_pages.empty())
        return;

	Page* page = nullptr;
    const auto sel_item = m_treectrl->GetSelection();
    const auto selection = sel_item ? m_treectrl->GetItemText(sel_item) : "";
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
//		if (el.get()->IsShown()) {
			el.get()->Hide();
//			break;
//		}

	#ifdef __linux__
	    no_updates.reset(nullptr);
	#endif

	update_undo_buttons();
	page->Show();
//	if (! page->layout_valid) {
		page->layout_valid = true;
		m_hsizer->Layout();
		Refresh();
//	}
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
		const Preset &preset = m_presets->get_selected_preset();
        auto default_name = preset.is_default ? "Untitled" :
                            preset.is_system ? (boost::format(_utf8(L("%1% - Copy"))) % preset.name).str() : 
	                        preset.name;

 		bool have_extention = boost::iends_with(default_name, ".ini");
		if (have_extention) {
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
		if (name == "") {
			show_error(this, _(L("The supplied name is empty. It can't be saved.")));
			return;
		}
		const Preset *existing = m_presets->find_preset(name, false);
		if (existing && (existing->is_default || existing->is_system)) {
			show_error(this, _(L("Cannot overwrite a system profile.")));
			return;
		}
		if (existing && (existing->is_external)) {
			show_error(this, _(L("Cannot overwrite an external profile.")));
			return;
		}
	}

	// Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
	m_presets->save_current_preset(name);
	// Mark the print & filament enabled if they are compatible with the currently selected preset.
	m_preset_bundle->update_compatible(false);
	// Add the new item into the UI component, remove dirty flags and activate the saved item.
	update_tab_ui();
	// Update the selection boxes at the platter.
	on_presets_changed();
	// If current profile is saved, "delete preset" button have to be enabled
	m_btn_delete_preset->Enable(true);

	if (m_type == Preset::TYPE_PRINTER)
		static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<TabPrinter*>(this)->m_extruders_count;
	update_changed_ui();
}

// Called for a currently selected preset.
void Tab::delete_preset()
{
	auto current_preset = m_presets->get_selected_preset();
	// Don't let the user delete the ' - default - ' configuration.
    std::string action = current_preset.is_external ? _utf8(L("remove")) : _utf8(L("delete"));
    // TRN  remove/delete
    const wxString msg = from_u8((boost::format(_utf8(L("Are you sure you want to %1% the selected preset?"))) % action).str());
	action = current_preset.is_external ? _utf8(L("Remove")) : _utf8(L("Delete"));
	// TRN  Remove/Delete
    wxString title = from_u8((boost::format(_utf8(L("%1% Preset"))) % action).str());  //action + _(L(" Preset"));
	if (current_preset.is_default ||
		wxID_YES != wxMessageDialog(parent(), msg, title, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal())
		return;
	// Select will handle of the preset dependencies, of saving & closing the depending profiles, and
	// finally of deleting the preset.
	this->select_preset("", true);
}

void Tab::toggle_show_hide_incompatible()
{
	m_show_incompatible_presets = !m_show_incompatible_presets;
	update_show_hide_incompatible_button();
	update_tab_ui();
}

void Tab::update_show_hide_incompatible_button()
{
	m_btn_hide_incompatible_presets->SetBitmap_(m_show_incompatible_presets ?
		m_bmp_show_incompatible_presets : m_bmp_hide_incompatible_presets);
	m_btn_hide_incompatible_presets->SetToolTip(m_show_incompatible_presets ?
		"Both compatible an incompatible presets are shown. Click to hide presets not compatible with the current printer." :
		"Only compatible presets are shown. Click to show both the presets compatible and not compatible with the current printer.");
}

void Tab::update_ui_from_settings()
{
	// Show the 'show / hide presets' button only for the print and filament tabs, and only if enabled
	// in application preferences.
	m_show_btn_incompatible_presets = wxGetApp().app_config->get("show_incompatible_presets")[0] == '1' ? true : false;
	bool show = m_show_btn_incompatible_presets && m_type != Slic3r::Preset::TYPE_PRINTER;
	Layout();
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
wxSizer* Tab::compatible_widget_create(wxWindow* parent, PresetDependencies &deps)
{
	deps.checkbox = new wxCheckBox(parent, wxID_ANY, _(L("All")));
	deps.checkbox->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    add_scaled_button(parent, &deps.btn, "printer_white", wxString::Format(" %s %s", _(L("Set")), dots), wxBU_LEFT | wxBU_EXACTFIT);
    deps.btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add((deps.checkbox), 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add((deps.btn), 0, wxALIGN_CENTER_VERTICAL);

	deps.checkbox->Bind(wxEVT_CHECKBOX, ([this, &deps](wxCommandEvent e)
	{
		deps.btn->Enable(! deps.checkbox->GetValue());
		// All printers have been made compatible with this preset.
		if (deps.checkbox->GetValue())
			this->load_key_value(deps.key_list, std::vector<std::string> {});
		this->get_field(deps.key_condition)->toggle(deps.checkbox->GetValue());
		this->update_changed_ui();
	}) );

	deps.btn->Bind(wxEVT_BUTTON, ([this, parent, &deps](wxCommandEvent e)
	{
		// Collect names of non-default non-external profiles.
		PrinterTechnology printer_technology = m_preset_bundle->printers.get_edited_preset().printer_technology();
		PresetCollection &depending_presets  = (deps.type == Preset::TYPE_PRINTER) ? m_preset_bundle->printers :
				(printer_technology == ptFFF) ? m_preset_bundle->prints : m_preset_bundle->sla_prints;
		wxArrayString presets;
		for (size_t idx = 0; idx < depending_presets.size(); ++ idx)
		{
			Preset& preset = depending_presets.preset(idx);
			bool add = ! preset.is_default && ! preset.is_external;
			if (add && deps.type == Preset::TYPE_PRINTER)
				// Only add printers with the same technology as the active printer.
				add &= preset.printer_technology() == printer_technology;
			if (add)
				presets.Add(from_u8(preset.name));
		}

		wxMultiChoiceDialog dlg(parent, deps.dialog_title, deps.dialog_label, presets);
		// Collect and set indices of depending_presets marked as compatible.
		wxArrayInt selections;
		auto *compatible_printers = dynamic_cast<const ConfigOptionStrings*>(m_config->option(deps.key_list));
		if (compatible_printers != nullptr || !compatible_printers->values.empty())
			for (auto preset_name : compatible_printers->values)
				for (size_t idx = 0; idx < presets.GetCount(); ++idx)
					if (presets[idx] == preset_name) {
						selections.Add(idx);
						break;
					}
		dlg.SetSelections(selections);
		std::vector<std::string> value;
		// Show the dialog.
		if (dlg.ShowModal() == wxID_OK) {
			selections.Clear();
			selections = dlg.GetSelections();
			for (auto idx : selections)
				value.push_back(presets[idx].ToUTF8().data());
			if (value.empty()) {
				deps.checkbox->SetValue(1);
				deps.btn->Disable();
			}
			// All depending_presets have been made compatible with this preset.
			this->load_key_value(deps.key_list, value);
			this->update_changed_ui();
		}
	}));
	return sizer;
}

void Tab::compatible_widget_reload(PresetDependencies &deps)
{
	bool has_any = ! m_config->option<ConfigOptionStrings>(deps.key_list)->values.empty();
	has_any ? deps.btn->Enable() : deps.btn->Disable();
	deps.checkbox->SetValue(! has_any);
	this->get_field(deps.key_condition)->toggle(! has_any);
}

void Tab::fill_icon_descriptions()
{
	m_icon_descriptions.emplace_back(&m_bmp_value_lock, L("LOCKED LOCK"),
        // TRN Description for "LOCKED LOCK"
		L("indicates that the settings are the same as the system (or default) values for the current option group"));

    m_icon_descriptions.emplace_back(&m_bmp_value_unlock, L("UNLOCKED LOCK"),
        // TRN Description for "UNLOCKED LOCK"
		L("indicates that some settings were changed and are not equal to the system (or default) values for "
		"the current option group.\n"
		"Click the UNLOCKED LOCK icon to reset all settings for current option group to "
		"the system (or default) values."));

    m_icon_descriptions.emplace_back(&m_bmp_white_bullet, L("WHITE BULLET"),
        // TRN Description for "WHITE BULLET"
        L("for the left button: \tindicates a non-system (or non-default) preset,\n"
		"for the right button: \tindicates that the settings hasn't been modified."));

    m_icon_descriptions.emplace_back(&m_bmp_value_revert, L("BACK ARROW"),
        // TRN Description for "BACK ARROW"
        L("indicates that the settings were changed and are not equal to the last saved preset for "
		"the current option group.\n"
		"Click the BACK ARROW icon to reset all settings for the current option group to "
		"the last saved preset."));
}

void Tab::set_tooltips_text()
{
	// --- Tooltip text for reset buttons (for whole options group)
	// Text to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
	m_ttg_value_lock =		_(L("LOCKED LOCK icon indicates that the settings are the same as the system (or default) values "
								"for the current option group"));
	m_ttg_value_unlock =	_(L("UNLOCKED LOCK icon indicates that some settings were changed and are not equal "
								"to the system (or default) values for the current option group.\n"
								"Click to reset all settings for current option group to the system (or default) values."));
	m_ttg_white_bullet_ns =	_(L("WHITE BULLET icon indicates a non system (or non default) preset."));
	m_ttg_non_system =		&m_ttg_white_bullet_ns;
	// Text to be shown on the "Undo user changes" button next to each input field.
	m_ttg_white_bullet =	_(L("WHITE BULLET icon indicates that the settings are the same as in the last saved "
								"preset for the current option group."));
	m_ttg_value_revert =	_(L("BACK ARROW icon indicates that the settings were changed and are not equal to "
								"the last saved preset for the current option group.\n"
								"Click to reset all settings for the current option group to the last saved preset."));

	// --- Tooltip text for reset buttons (for each option in group)
	// Text to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
	m_tt_value_lock =		_(L("LOCKED LOCK icon indicates that the value is the same as the system (or default) value."));
	m_tt_value_unlock =		_(L("UNLOCKED LOCK icon indicates that the value was changed and is not equal "
								"to the system (or default) value.\n"
								"Click to reset current value to the system (or default) value."));
	// 	m_tt_white_bullet_ns=	_(L("WHITE BULLET icon indicates a non system preset."));
	m_tt_non_system =		&m_ttg_white_bullet_ns;
	// Text to be shown on the "Undo user changes" button next to each input field.
	m_tt_white_bullet =		_(L("WHITE BULLET icon indicates that the value is the same as in the last saved preset."));
	m_tt_value_revert =		_(L("BACK ARROW icon indicates that the value was changed and is not equal to the last saved preset.\n"
								"Click to reset current value to the last saved preset."));
}

void Page::reload_config()
{
	for (auto group : m_optgroups)
		group->reload_config();
}

void Page::update_visibility(ConfigOptionMode mode)
{
    bool ret_val = false;
    for (auto group : m_optgroups)
        ret_val = group->update_visibility(mode) || ret_val;

    m_show = ret_val;
}

void Page::msw_rescale()
{
    for (auto group : m_optgroups)
        group->msw_rescale();
}

Field* Page::get_field(const t_config_option_key& opt_key, int opt_index /*= -1*/) const
{
	Field* field = nullptr;
	for (auto opt : m_optgroups) {
		field = opt->get_fieldc(opt_key, opt_index);
		if (field != nullptr)
			return field;
	}
	return field;
}

bool Page::set_value(const t_config_option_key& opt_key, const boost::any& value) {
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
    auto extra_column = [this](wxWindow* parent, const Line& line)
    {
        std::string bmp_name;
        const std::vector<Option>& options = line.get_options();
        int mode_id = int(options[0].opt.mode);
        const wxBitmap& bitmap = options.size() == 0 || options[0].opt.gui_type == "legend" ? wxNullBitmap :
                                 m_mode_bitmap_cache[mode_id].bmp();
        auto bmp = new wxStaticBitmap(parent, wxID_ANY, bitmap);
        bmp->SetClientData((void*)&m_mode_bitmap_cache[mode_id]);

        bmp->SetBackgroundStyle(wxBG_STYLE_PAINT);
        return bmp;
    };

	//! config_ have to be "right"
	ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(this, title, m_config, true, extra_column);
	if (noncommon_label_width >= 0)
		optgroup->label_width = noncommon_label_width;

#ifdef __WXOSX__
		auto tab = GetParent()->GetParent();
#else
		auto tab = GetParent();
#endif
	optgroup->m_on_change = [this, tab](t_config_option_key opt_key, boost::any value) {
		//! This function will be called from OptionGroup.
		//! Using of CallAfter is redundant.
		//! And in some cases it causes update() function to be recalled again
//!        wxTheApp->CallAfter([this, opt_key, value]() {
			static_cast<Tab*>(tab)->update_dirty();
			static_cast<Tab*>(tab)->on_value_change(opt_key, value);
//!        });
	};

	optgroup->m_get_initial_config = [this, tab]() {
		DynamicPrintConfig config = static_cast<Tab*>(tab)->m_presets->get_selected_preset().config;
		return config;
	};

	optgroup->m_get_sys_config = [this, tab]() {
		DynamicPrintConfig config = static_cast<Tab*>(tab)->m_presets->get_selected_preset_parent()->config;
		return config;
	};

	optgroup->have_sys_config = [this, tab]() {
		return static_cast<Tab*>(tab)->m_presets->get_selected_preset_parent() != nullptr;
	};

    optgroup->rescale_extra_column_item = [this](wxWindow* win) {
        auto *ctrl = dynamic_cast<wxStaticBitmap*>(win);
        if (ctrl == nullptr)
            return;

        ctrl->SetBitmap(reinterpret_cast<ScalableBitmap*>(ctrl->GetClientData())->bmp());
    };

	vsizer()->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 10);
	m_optgroups.push_back(optgroup);

	return optgroup;
}

void SavePresetWindow::build(const wxString& title, const std::string& default_name, std::vector<std::string> &values)
{
    // TRN Preset
	auto text = new wxStaticText(this, wxID_ANY, wxString::Format(_(L("Save %s as:")), title), 
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
		const char* unusable_symbols = "<>[]:/\\|?*\"";
		bool is_unusable_symbol = false;
		bool is_unusable_suffix = false;
		const std::string unusable_suffix = PresetCollection::get_suffix_modified();//"(modified)";
		for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
			if (m_chosen_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
				is_unusable_symbol = true;
				break;
			}
		}
		if (m_chosen_name.find(unusable_suffix) != std::string::npos)
			is_unusable_suffix = true;

		if (is_unusable_symbol) {
			show_error(this,_(L("The supplied name is not valid;")) + "\n" +
							_(L("the following characters are not allowed:")) + " " + unusable_symbols);
		}
		else if (is_unusable_suffix) {
			show_error(this,_(L("The supplied name is not valid;")) + "\n" +
							_(L("the following suffix is not allowed:")) + "\n\t" +
							wxString::FromUTF8(unusable_suffix.c_str()));
		}
		else if (m_chosen_name == "- default -") {
			show_error(this, _(L("The supplied name is not available.")));
		}
		else {
			EndModal(wxID_OK);
		}
	}
}

void TabSLAMaterial::build()
{
    m_presets = &m_preset_bundle->sla_materials;
    load_initial_data();

    auto page = add_options_page(_(L("Material")), "resin");

    auto optgroup = page->new_optgroup(_(L("Layers")));
//     optgroup->append_single_option_line("layer_height");
    optgroup->append_single_option_line("initial_layer_height");

    optgroup = page->new_optgroup(_(L("Exposure")));
    optgroup->append_single_option_line("exposure_time");
    optgroup->append_single_option_line("initial_exposure_time");

    optgroup = page->new_optgroup(_(L("Corrections")));
    optgroup->label_width = 19;//190;
    std::vector<std::string> corrections = {"material_correction"};
//    std::vector<std::string> axes{ "X", "Y", "Z" };
    std::vector<std::string> axes{ "XY", "Z" };
    for (auto& opt_key : corrections) {
        auto line = Line{ _(m_config->def()->get(opt_key)->full_label), "" };
        int id = 0;
        for (auto& axis : axes) {
            auto opt = optgroup->get_option(opt_key, id);
            opt.opt.label = axis;
            opt.opt.width = 6;
            line.append_option(opt);
            ++id;
        }
        optgroup->append_line(line);
    }

    page = add_options_page(_(L("Notes")), "note.png");
    optgroup = page->new_optgroup(_(L("Notes")), 0);
    optgroup->label_width = 0;
    Option option = optgroup->get_option("material_notes");
    option.opt.full_width = true;
    option.opt.height = 25;//250;
    optgroup->append_single_option_line(option);

    page = add_options_page(_(L("Dependencies")), "wrench.png");
    optgroup = page->new_optgroup(_(L("Profile dependencies")));
    Line line = optgroup->create_single_option_line("compatible_printers");
    line.widget = [this](wxWindow* parent) {
        return compatible_widget_create(parent, m_compatible_printers);
    };
    optgroup->append_line(line, &m_colored_Label);
    option = optgroup->get_option("compatible_printers_condition");
    option.opt.full_width = true;
    optgroup->append_single_option_line(option);

    line = optgroup->create_single_option_line("compatible_prints");
    line.widget = [this](wxWindow* parent) {
		return compatible_widget_create(parent, m_compatible_prints);
	};
	optgroup->append_line(line, &m_colored_Label);
	option = optgroup->get_option("compatible_prints_condition");
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
void TabSLAMaterial::reload_config()
{
	this->compatible_widget_reload(m_compatible_printers);
	this->compatible_widget_reload(m_compatible_prints);
	Tab::reload_config();
}

void TabSLAMaterial::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF)
        return;
    
// #ys_FIXME. Just a template for this function
//     m_update_cnt++;
//     ! something to update
//     m_update_cnt--;
// 
//     if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabSLAPrint::build()
{
    m_presets = &m_preset_bundle->sla_prints;
    load_initial_data();

    auto page = add_options_page(_(L("Layers and perimeters")), "layers");

    auto optgroup = page->new_optgroup(_(L("Layers")));
    optgroup->append_single_option_line("layer_height");
    optgroup->append_single_option_line("faded_layers");

    page = add_options_page(_(L("Supports")), "support"/*"sla_supports"*/);
    optgroup = page->new_optgroup(_(L("Supports")));
    optgroup->append_single_option_line("supports_enable");

    optgroup = page->new_optgroup(_(L("Support head")));
    optgroup->append_single_option_line("support_head_front_diameter");
    optgroup->append_single_option_line("support_head_penetration");
    optgroup->append_single_option_line("support_head_width");

    optgroup = page->new_optgroup(_(L("Support pillar")));
    optgroup->append_single_option_line("support_pillar_diameter");
    optgroup->append_single_option_line("support_pillar_connection_mode");
    optgroup->append_single_option_line("support_buildplate_only");
    // TODO: This parameter is not used at the moment.
    // optgroup->append_single_option_line("support_pillar_widening_factor");
    optgroup->append_single_option_line("support_base_diameter");
    optgroup->append_single_option_line("support_base_height");
    optgroup->append_single_option_line("support_base_safety_distance");
    optgroup->append_single_option_line("support_object_elevation");

    optgroup = page->new_optgroup(_(L("Connection of the support sticks and junctions")));
    optgroup->append_single_option_line("support_critical_angle");
    optgroup->append_single_option_line("support_max_bridge_length");
    optgroup->append_single_option_line("support_max_pillar_link_distance");

    optgroup = page->new_optgroup(_(L("Automatic generation")));
    optgroup->append_single_option_line("support_points_density_relative");
    optgroup->append_single_option_line("support_points_minimal_distance");

    page = add_options_page(_(L("Pad")), "pad");
    optgroup = page->new_optgroup(_(L("Pad")));
    optgroup->append_single_option_line("pad_enable");
    optgroup->append_single_option_line("pad_wall_thickness");
    optgroup->append_single_option_line("pad_wall_height");
    optgroup->append_single_option_line("pad_max_merge_distance");
    // TODO: Disabling this parameter for the beta release
//    optgroup->append_single_option_line("pad_edge_radius");
    optgroup->append_single_option_line("pad_wall_slope");
    
    optgroup->append_single_option_line("pad_object_gap");
    optgroup->append_single_option_line("pad_object_connector_stride");
    optgroup->append_single_option_line("pad_object_connector_width");
    optgroup->append_single_option_line("pad_object_connector_penetration");
    
	page = add_options_page(_(L("Advanced")), "wrench");
	optgroup = page->new_optgroup(_(L("Slicing")));
	optgroup->append_single_option_line("slice_closing_radius");

	page = add_options_page(_(L("Output options")), "output+page_white");
	optgroup = page->new_optgroup(_(L("Output file")));
	Option option = optgroup->get_option("output_filename_format");
	option.opt.full_width = true;
	optgroup->append_single_option_line(option);

    page = add_options_page(_(L("Dependencies")), "wrench");
    optgroup = page->new_optgroup(_(L("Profile dependencies")));
    Line line = optgroup->create_single_option_line("compatible_printers");//Line { _(L("Compatible printers")), "" };
    line.widget = [this](wxWindow* parent) {
        return compatible_widget_create(parent, m_compatible_printers);
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
void TabSLAPrint::reload_config()
{
	this->compatible_widget_reload(m_compatible_printers);
	Tab::reload_config();
}

void TabSLAPrint::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF)
        return;

     m_update_cnt++;

    double head_penetration = m_config->opt_float("support_head_penetration");
    double head_width       = m_config->opt_float("support_head_width");
    if (head_penetration > head_width) {
        wxString msg_text = _(
            L("Head penetration should not be greater than the head width."));

        auto dialog = new wxMessageDialog(parent(),
                                          msg_text,
                                          _(L("Invalid Head penetration")),
                                          wxICON_WARNING | wxOK);

        DynamicPrintConfig new_conf = *m_config;
        if (dialog->ShowModal() == wxID_OK) {
            new_conf.set_key_value("support_head_penetration",
                                   new ConfigOptionFloat(head_width));
        }

        load_config(new_conf);
    }

    double pinhead_d = m_config->opt_float("support_head_front_diameter");
    double pillar_d  = m_config->opt_float("support_pillar_diameter");
    if (pinhead_d > pillar_d) {
        wxString msg_text = _(L(
            "Pinhead diameter should be smaller than the pillar diameter."));

        auto dialog = new wxMessageDialog(parent(),
                                          msg_text,
                                          _(L("Invalid pinhead diameter")),
                                          wxICON_WARNING | wxOK);

        DynamicPrintConfig new_conf = *m_config;
        if (dialog->ShowModal() == wxID_OK) {
            new_conf.set_key_value("support_head_front_diameter",
                                   new ConfigOptionFloat(pillar_d / 2.0));
        }

        load_config(new_conf);
    }
    
    // if(m_config->opt_float("support_object_elevation") < EPSILON &&
    //    m_config->opt_bool("pad_enable")) {
    //     // TODO: disable editding of:
    //     // pad_object_connector_stride
    //     // pad_object_connector_width
    //     // pad_object_connector_penetration
    // }

    m_update_cnt--;

    if (m_update_cnt == 0) wxGetApp().mainframe->on_config_changed(m_config);
}

} // GUI
} // Slic3r
