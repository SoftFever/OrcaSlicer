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
class CPage : public wxScrolledWindow
{
	wxWindow*		parent_;
	wxString		title_;
	size_t			iconID_;
	wxBoxSizer*		vsizer_;
public:
	CPage(wxWindow* parent, const wxString title, const int iconID) :
			parent_(parent), 
			title_(title), 
			iconID_(iconID)
	{
		Create(parent_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);		
		vsizer_ = new wxBoxSizer(wxVERTICAL);
		SetSizer(vsizer_);
	}
	~CPage(){}

public:
	std::vector <ConfigOptionsGroupShp> optgroups;  // $self->{optgroups} = [];
	DynamicPrintConfig* config_;

	wxBoxSizer*	vsizer() const { return vsizer_; }	
	wxWindow*	parent() const { return parent_; }
	wxString	title()	 const { return title_; }
	size_t		iconID() const { return iconID_; }
	void		set_config(DynamicPrintConfig* config_in) { config_ = config_in; }

	ConfigOptionsGroupShp new_optgroup(std::string title, size_t label_width = 0);
};

// Slic3r::GUI::Tab;

using CPageShp = std::shared_ptr<CPage>;
class CTab: public wxPanel
{
	wxNotebook*			parent_;
protected:
	const char*			title_;
	wxBitmapComboBox*	presets_choice_;
	wxBitmapButton*		btn_save_preset_;
	wxBitmapButton*		btn_delete_preset_;
	wxBitmap*			bmp_show_incompatible_presets_;
	wxBitmap*			bmp_hide_incompatible_presets_;
	wxBitmapButton*		btn_hide_incompatible_presets_;
	wxBoxSizer*			hsizer_;
	wxBoxSizer*			left_sizer_;
	wxTreeCtrl*			treectrl_;
	wxImageList*		icons_;
	wxCheckBox*			compatible_printers_checkbox_;
	wxButton*			compatible_printers_btn;
	int					icon_count;
	std::map<wxString, size_t>				icon_index_;		// Map from an icon file name to its index in $self->{icons}.
	std::vector<CPageShp>		pages_;	// $self->{pages} = [];
	bool				disable_tree_sel_changed_event_;

public:
	DynamicPrintConfig config_;		//! tmp_val
	const ConfigDef* config_def;	//! tmp_val

public:
	CTab() {}
	CTab(wxNotebook* parent, const char *title) : parent_(parent), title_(title) { 
		Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
	}
	~CTab(){}

	wxWindow*	parent() const { return parent_; }
	
	void		create_preset_tab();
	void		rebuild_page_tree();
	void		select_preset(wxString preset_name){};

	static wxSizer*	compatible_printers_widget_(wxWindow* parent);
	void		load_key_value_(std::string opt_key, std::vector<std::string> value);

	void		OnTreeSelChange(wxTreeEvent& event);
	void		OnKeyDown(wxKeyEvent& event);
	void		OnComboBox(wxCommandEvent& event) { select_preset(presets_choice_->GetStringSelection()); 	}
	void		save_preset(wxCommandEvent &event);
	void		delete_preset(wxCommandEvent &event);
	void		_toggle_show_hide_incompatible(wxCommandEvent &event);

	std::shared_ptr<CPage> add_options_page(wxString title, wxString icon);

	virtual void build() = 0;
//	virtual void _update();

	Option get_option_(const std::string title){
		return Option(*config_def->get(title), title);
	}
};

//Slic3r::GUI::Tab::Print;
class CTabPrint : public CTab
{
public:
	CTabPrint() {}
	CTabPrint(wxNotebook* parent, const char *title/*, someParams*/) : CTab(parent, title) {}
	~CTabPrint(){}

	void  build() override;
};

} // GUI
} // Slic3r
