#ifndef slic3r_ButtonsDescription_hpp
#define slic3r_ButtonsDescription_hpp

#include <wx/dialog.h>
#include <vector>

class ScalableBitmap;
class wxColourPickerCtrl;

namespace Slic3r {
namespace GUI {

class ButtonsDescription : public wxDialog
{
	wxColourPickerCtrl* sys_colour{ nullptr };
	wxColourPickerCtrl* mod_colour{ nullptr };
public:
	struct Entry {
		Entry(ScalableBitmap *bitmap, const std::string &symbol, const std::string &explanation) : bitmap(bitmap), symbol(symbol), explanation(explanation) {}

		ScalableBitmap *bitmap;
		std::string     symbol;
		std::string   	explanation;
	};

	ButtonsDescription(wxWindow* parent, const std::vector<Entry> &entries);
	~ButtonsDescription() {}

	static void FillSizerWithTextColorDescriptions(wxSizer* sizer, wxWindow* parent, wxColourPickerCtrl** sys_colour, wxColourPickerCtrl** mod_colour);

private:
	std::vector<Entry> m_entries;
};

} // GUI
} // Slic3r


#endif 

