#include "ButtonsDescription.hpp"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>

#include "GUI.hpp"

namespace Slic3r {
namespace GUI {

ButtonsDescription::ButtonsDescription(wxWindow* parent, t_icon_descriptions* icon_descriptions) :
	wxDialog(parent, wxID_ANY, "Buttons Description", wxDefaultPosition, wxDefaultSize),
	m_icon_descriptions(icon_descriptions)
{
	auto grid_sizer = new wxFlexGridSizer(3, 20, 20);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(grid_sizer, 0, wxEXPAND | wxALL, 20);

	for (auto pair : *m_icon_descriptions)
	{
		auto icon = new wxStaticBitmap(this, wxID_ANY, *pair.first);
		grid_sizer->Add(icon, -1, wxALIGN_CENTRE_VERTICAL);

		std::istringstream f(pair.second);
		std::string s;
		while (getline(f, s, ';')) {
			auto description = new wxStaticText(this, wxID_ANY, _(s));
			grid_sizer->Add(description, -1, wxALIGN_CENTRE_VERTICAL);
		}
	}

	auto button = CreateStdDialogButtonSizer(wxOK);
	main_sizer->Add(button, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	SetSizer(main_sizer);
	main_sizer->SetSizeHints(this);
}

} // GUI
} // Slic3r

