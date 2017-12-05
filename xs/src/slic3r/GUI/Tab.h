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

#include <map>
#include <vector>

namespace Slic3r {
namespace GUI {

// Single Tab page containing a{ vsizer } of{ optgroups }
// package Slic3r::GUI::Tab::Page;
class CPage : public wxScrolledWindow
{
	const char*	_title;
	wxWindow*	_parent;
	wxBoxSizer*	_vsizer;
	size_t		_iconID;
//	const OptionsGroup opt;  // $self->{optgroups} = [];
public:
	CPage(){};
	CPage(wxWindow* parent, const char* title, int iconID) :
			_parent(parent), 
			_title(title), 
			_iconID(iconID)
	{
		Create(_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
		this->SetScrollbars(1, 1, 1, 1);
		_vsizer = new wxBoxSizer(wxVERTICAL);
		this->SetSizer(_vsizer);
	}
	~CPage(){};
	wxBoxSizer * vsizer(){ return this->_vsizer; }	
	wxString title(){ return wxString(_title); }
};

// Slic3r::GUI::Tab;
class CTab: public wxPanel
{
protected:
	const char* _title;
	wxBitmapComboBox*	presets_choice;
	wxBitmap*	bmp_show_incompatible_presets;
	wxBitmap*	bmp_hide_incompatible_presets;
	wxBitmapButton*		btn_hide_incompatible_presets;
	wxBoxSizer*	hsizer;
	wxBoxSizer*	left_sizer;
	wxTreeCtrl*	treectrl;
	wxImageList*	icons;
	int			icon_count;
//	std::map<size_t, wxImageList*> icon_index;
	std::vector<CPage>	pages;
	bool	disable_tree_sel_changed_event;
public:
	CTab(){};
	CTab(wxNotebook* parent, const char *title/*, someParams*/){}
	~CTab(){};
	
	void create_preset_tab();
	void OnTreeSelChange(wxCommandEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnComboBox(wxCommandEvent& event){ /*$self->select_preset(presets_choice->GetStringSelection)*/ };
	void save_preset(wxCommandEvent &event);
	void delete_preset(wxCommandEvent &event);
	void _toggle_show_hide_incompatible(wxCommandEvent &event);

//	virtual void build(/*%params*/);
//	virtual void rebuild_page_tree();
//	virtual void _update();
private:
//	DECLARE_EVENT_TABLE()
};

//Slic3r::GUI::Tab::Print;
class CTabPrint : public CTab
{
public:
	CTabPrint() {};
	CTabPrint(wxNotebook* parent, const char *title/*, someParams*/)
	{
		_title = title;
		Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
		create_preset_tab();
	}
	~CTabPrint(){};
//	void  build(/*%params*/){};
};

} // GUI
} // Slic3r
