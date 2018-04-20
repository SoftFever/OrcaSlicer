#ifndef slic3r_BedShapeDialog_hpp_
#define slic3r_BedShapeDialog_hpp_
// The bed shape dialog.
// The dialog opens from Print Settins tab->Bed Shape : Set...

#include "OptionsGroup.hpp"
#include "2DBed.hpp"


#include <wx/dialog.h>
#include <wx/choicebk.h>

namespace Slic3r {
namespace GUI {

using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
class BedShapePanel : public wxPanel
{
	wxChoicebook*	m_shape_options_book;
	Bed_2D*			m_canvas;

	std::vector <ConfigOptionsGroupShp>	m_optgroups;

public:
	BedShapePanel(wxWindow* parent) : wxPanel(parent, wxID_ANY){}
	~BedShapePanel(){}

	void		build_panel(ConfigOptionPoints* default_pt);
	
	ConfigOptionsGroupShp	init_shape_options_page(wxString title);
	void		set_shape(ConfigOptionPoints* points);
	void		update_preview();
	void		update_shape();
	void		load_stl();
	
	// Returns the resulting bed shape polygon. This value will be stored to the ini file.
	std::vector<Pointf>	GetValue() { return m_canvas->m_bed_shape; }
};

class BedShapeDialog : public wxDialog
{
	BedShapePanel*	m_panel;
public:
	BedShapeDialog(wxWindow* parent) : wxDialog(parent, wxID_ANY, _(L("Bed Shape")),
		wxDefaultPosition, wxSize(350, 700), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER){}
	~BedShapeDialog(){  }

	void		build_dialog(ConfigOptionPoints* default_pt);
	std::vector<Pointf>	GetValue() { return m_panel->GetValue(); }
};

} // GUI
} // Slic3r


#endif  /* slic3r_BedShapeDialog_hpp_ */
