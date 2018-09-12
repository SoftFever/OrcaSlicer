#ifndef slic3r_LambdaObjectDialog_hpp_
#define slic3r_LambdaObjectDialog_hpp_

#include "GUI.hpp"

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/choicebk.h>

namespace Slic3r
{
namespace GUI
{
using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
class LambdaObjectDialog : public wxDialog
{
	wxChoicebook*	m_modificator_options_book;
	std::vector <ConfigOptionsGroupShp>	m_optgroups;
public:
	LambdaObjectDialog(wxWindow* parent);
	~LambdaObjectDialog(){}

	bool CanClose() { return true; }	// ???
	OBJECT_PARAMETERS& ObjectParameters(){ return object_parameters; }

	ConfigOptionsGroupShp init_modificator_options_page(wxString title);
	
	// Note whether the window was already closed, so a pending update is not executed.
	bool m_already_closed = false;
	OBJECT_PARAMETERS object_parameters;
	wxBoxSizer* sizer = nullptr;
};
} //namespace GUI
} //namespace Slic3r 
#endif  //slic3r_LambdaObjectDialog_hpp_
