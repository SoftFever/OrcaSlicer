#ifndef slic3r_ButtonsDescription_hpp
#define slic3r_ButtonsDescription_hpp

#include <wx/dialog.h>
#include <vector>

class ScalableBitmap;

namespace Slic3r {
namespace GUI {

class ButtonsDescription : public wxDialog
{
public:
	struct Entry {
		Entry(ScalableBitmap *bitmap, const std::string &symbol, const std::string &explanation) : bitmap(bitmap), symbol(symbol), explanation(explanation) {}

		ScalableBitmap *bitmap;
		std::string     symbol;
		std::string   	explanation;
	};

	ButtonsDescription(wxWindow* parent, const std::vector<Entry> &entries);
	~ButtonsDescription() {}

private:
	std::vector<Entry> m_entries;
};

} // GUI
} // Slic3r


#endif 

