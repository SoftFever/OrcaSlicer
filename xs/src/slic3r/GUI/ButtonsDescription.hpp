#ifndef slic3r_ButtonsDescription_hpp
#define slic3r_ButtonsDescription_hpp

#include <wx/dialog.h>
#include <vector>

namespace Slic3r {
namespace GUI {

using t_icon_descriptions = std::vector<std::pair<wxBitmap*, std::string>>;

class ButtonsDescription : public wxDialog
{
	t_icon_descriptions* m_icon_descriptions;
public:
	ButtonsDescription(wxWindow* parent, t_icon_descriptions* icon_descriptions);
	~ButtonsDescription(){}


};

} // GUI
} // Slic3r


#endif 

