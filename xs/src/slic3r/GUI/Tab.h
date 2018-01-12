//	 The "Expert" tab at the right of the main tabbed window.
//	
//	 This file implements following packages:
//	   Slic3r::GUI::Tab;
//	       Slic3r::GUI::Tab::Print;
//	       Slic3r::GUI::Tab::Filament;
//	       Slic3r::GUI::Tab::Printer;
//	   Slic3r::GUI::Tab::Page
//	       - Option page: For example, the Slic3r::GUI::Tab::Print has option pages "Layers and perimeters", "Infill", "Skirt and brim" ...
//	   Slic3r::GUI::SavePresetWindow
//	       - Dialog to select a new preset name to store the configuration.
//	   Slic3r::GUI::Tab::Preset;
//	       - Single preset item: name, file is default or external.

#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/statbox.h>

#include <map>
#include <vector>
#include <memory>

#include "OptionsGroup.hpp"

//!enum { ID_TAB_TREE = wxID_HIGHEST + 1 };

namespace Slic3r {
namespace GUI {

// Single Tab page containing a{ vsizer } of{ optgroups }
// package Slic3r::GUI::Tab::Page;
using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
class Page : public wxScrolledWindow
{
	wxWindow*		m_parent;
	wxString		m_title;
	size_t			m_iconID;
	wxBoxSizer*		m_vsizer;
public:
	Page(wxWindow* parent, const wxString title, const int iconID) :
			m_parent(parent),
			m_title(title),
			m_iconID(iconID)
	{
		Create(m_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
		m_vsizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(m_vsizer);
	}
	~Page(){}

public:
	std::vector <ConfigOptionsGroupShp> m_optgroups;  // $self->{optgroups} = [];
	DynamicPrintConfig* m_config;

	wxBoxSizer*	vsizer() const { return m_vsizer; }
	wxWindow*	parent() const { return m_parent; }
	wxString	title()	 const { return m_title; }
	size_t		iconID() const { return m_iconID; }
	void		set_config(DynamicPrintConfig* config_in) { m_config = config_in; }
	void		reload_config();
	Field*		get_field(t_config_option_key opt_key, int opt_index = -1) const;

	ConfigOptionsGroupShp new_optgroup(std::string title, int noncommon_label_width = -1);
};

// Slic3r::GUI::Tab;

using PageShp = std::shared_ptr<Page>;
class Tab: public wxPanel
{
	wxNotebook*			m_parent;
protected:
	const wxString		m_title;
	wxBitmapComboBox*	m_presets_choice;
	wxBitmapButton*		m_btn_save_preset;
	wxBitmapButton*		m_btn_delete_preset;
	wxBitmap*			m_bmp_show_incompatible_presets;
	wxBitmap*			m_bmp_hide_incompatible_presets;
	wxBitmapButton*		m_btn_hide_incompatible_presets;
	wxBoxSizer*			m_hsizer;
	wxBoxSizer*			m_left_sizer;
	wxTreeCtrl*			m_treectrl;
	wxImageList*		m_icons;
	wxCheckBox*			m_compatible_printers_checkbox;
	wxButton*			m_compatible_printers_btn;

	int					m_icon_count;
	std::map<std::string, size_t>	m_icon_index;		// Map from an icon file name to its index in $self->{icons}.
	std::vector<PageShp>			m_pages;	// $self->{pages} = [];
	bool				m_disable_tree_sel_changed_event;

public:
	PresetBundle*		m_preset_bundle;
	bool				m_no_controller;
	PresetCollection*	m_presets;
	DynamicPrintConfig*	m_config;		//! tmp_val
	const ConfigDef*	m_config_def;	// It will be used in get_option_(const std::string title)
	t_change			m_on_value_change{ nullptr };

public:
	Tab() {}
	Tab(wxNotebook* parent, const char *title) : m_parent(parent), m_title(title) {
		Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
	}
	~Tab(){}

	wxWindow*	parent() const { return m_parent; }
	wxString	title()	 const { return m_title; }
	
	void		create_preset_tab(PresetBundle *preset_bundle);
	void		on_value_change(std::string opt_key, boost::any value){
		if (m_on_value_change != nullptr)
			m_on_value_change(opt_key, value);
		update();
	};
	void		on_presets_changed(){};
	void		load_current_preset();
	void		rebuild_page_tree();
	void		select_preset(wxString preset_name){};

	wxSizer*	compatible_printers_widget(wxWindow* parent, wxCheckBox* checkbox, wxButton* btn);

	void		load_key_value(std::string opt_key, boost::any value);
	void		reload_compatible_printers_widget();

	void		OnTreeSelChange(wxTreeEvent& event);
	void		OnKeyDown(wxKeyEvent& event);
	void		OnComboBox(wxCommandEvent& event) { select_preset(m_presets_choice->GetStringSelection()); 	}
	void		save_preset(wxCommandEvent &event);
	void		delete_preset(wxCommandEvent &event);
	void		toggle_show_hide_incompatible(wxCommandEvent &event);

	PageShp		add_options_page(wxString title, std::string icon, bool is_extruder_pages = false);

	virtual void	OnActivate(){};
	virtual void	build() = 0;
	virtual void	update() = 0;
	void			update_dirty();
	void			load_config(DynamicPrintConfig config);
	virtual void	reload_config();
	Field*			get_field(t_config_option_key opt_key, int opt_index = -1) const;
};

//Slic3r::GUI::Tab::Print;
class TabPrint : public Tab
{
public:
	TabPrint() {}
	TabPrint(wxNotebook* parent, const char *title) : Tab(parent, title) {}
	~TabPrint(){}

	bool		m_support_material_overhangs_queried = false;

	void		build() override;
	void		reload_config() override;
	void		update() override;
};

//Slic3r::GUI::Tab::Filament;
class TabFilament : public Tab
{
	wxStaticText*	m_volumetric_speed_description_line;
	wxStaticText*	m_cooling_description_line;
public:
	TabFilament() {}
	TabFilament(wxNotebook* parent, const char *title) : Tab(parent, title) {}
	~TabFilament(){}

	wxSizer*		description_line_widget(wxWindow* parent, wxStaticText* StaticText);

	void		build() override;
	void		reload_config() override;
	void		update() override;

	void		OnActivate() override;
};

//Slic3r::GUI::Tab::Printer;
class TabPrinter : public Tab
{
public:
	wxButton*	serial_test_btn;
	wxButton*	octoprint_host_test_btn;

	size_t		m_extruders_count;

public:
	TabPrinter() {}
	TabPrinter(wxNotebook* parent, const char *title) : Tab(parent, title) {}
	~TabPrinter(){}

	void		build() override;
	void		update() override{};
	void		build_extruder_pages();
};

} // GUI
} // Slic3r
