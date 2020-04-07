#ifndef slic3r_LambdaObjectDialog_hpp_
#define slic3r_LambdaObjectDialog_hpp_

#include <vector>
#include <memory>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/choicebk.h>

class wxPanel;

namespace Slic3r
{
namespace GUI
{
enum LambdaTypeIDs{
    LambdaTypeBox,
    LambdaTypeCylinder,
    LambdaTypeSphere,
    LambdaTypeSlab
};

struct OBJECT_PARAMETERS
{
    LambdaTypeIDs	type = LambdaTypeBox;
    double			dim[3];// = { 1.0, 1.0, 1.0 };
    int				cyl_r = 1;
    int				cyl_h = 1;
    double			sph_rho = 1.0;
    double			slab_h = 1.0;
    double			slab_z = 0.0;
};
class ConfigOptionsGroup;
using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
class LambdaObjectDialog : public wxDialog
{
	wxChoicebook*	m_modificator_options_book = nullptr;
	std::vector <ConfigOptionsGroupShp>	m_optgroups;
    wxString        m_type_name;
    wxPanel*        m_panel = nullptr;
public:
    LambdaObjectDialog(wxWindow* parent, 
                       const wxString type_name = wxEmptyString);
	~LambdaObjectDialog() {}

	bool CanClose() { return true; }	// ???
	OBJECT_PARAMETERS& ObjectParameters() { return object_parameters; }

	ConfigOptionsGroupShp init_modificator_options_page(const wxString& title);
	
	// Note whether the window was already closed, so a pending update is not executed.
	bool m_already_closed = false;
	OBJECT_PARAMETERS object_parameters;
	wxBoxSizer* sizer = nullptr;
};
} //namespace GUI
} //namespace Slic3r 
#endif  //slic3r_LambdaObjectDialog_hpp_
