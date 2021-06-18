#include "ButtonsDescription.hpp"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>
#include <wx/clrpicker.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

ButtonsDescription::ButtonsDescription(wxWindow* parent, const std::vector<Entry> &entries) :
	wxDialog(parent, wxID_ANY, _(L("Buttons And Text Colors Description")), wxDefaultPosition, wxDefaultSize),
	m_entries(entries)
{
	wxGetApp().UpdateDarkUI(this);

	auto grid_sizer = new wxFlexGridSizer(3, 20, 20);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(grid_sizer, 0, wxEXPAND | wxALL, 20);

	// Icon description
	for (const Entry &entry : m_entries)
	{
		auto icon = new wxStaticBitmap(this, wxID_ANY, entry.bitmap->bmp());
		grid_sizer->Add(icon, -1, wxALIGN_CENTRE_VERTICAL);
		auto description = new wxStaticText(this, wxID_ANY, _(entry.symbol));
		grid_sizer->Add(description, -1, wxALIGN_CENTRE_VERTICAL);
		description = new wxStaticText(this, wxID_ANY, _(entry.explanation));
		grid_sizer->Add(description, -1, wxALIGN_CENTRE_VERTICAL | wxEXPAND);
	}

	// Text color description
	auto sys_label = new wxStaticText(this, wxID_ANY, _(L("Value is the same as the system value")));
	sys_label->SetForegroundColour(wxGetApp().get_label_clr_sys());
	auto sys_colour = new wxColourPickerCtrl(this, wxID_ANY, wxGetApp().get_label_clr_sys());
	wxGetApp().UpdateDarkUI(sys_colour->GetPickerCtrl(), true);
	sys_colour->Bind(wxEVT_COLOURPICKER_CHANGED, ([sys_colour, sys_label](wxCommandEvent e)
	{
		sys_label->SetForegroundColour(sys_colour->GetColour());
		sys_label->Refresh();
	}));
	size_t t= 0;
	while (t < 3) {
		grid_sizer->Add(new wxStaticText(this, wxID_ANY, ""), -1, wxALIGN_CENTRE_VERTICAL | wxEXPAND);
		++t;
	}
	grid_sizer->Add(0, -1, wxALIGN_CENTRE_VERTICAL);
	grid_sizer->Add(sys_colour, -1, wxALIGN_CENTRE_VERTICAL);
	grid_sizer->Add(sys_label, -1, wxALIGN_CENTRE_VERTICAL | wxEXPAND);

	auto mod_label = new wxStaticText(this, wxID_ANY, _(L("Value was changed and is not equal to the system value or the last saved preset")));
	mod_label->SetForegroundColour(wxGetApp().get_label_clr_modified());
	auto mod_colour = new wxColourPickerCtrl(this, wxID_ANY, wxGetApp().get_label_clr_modified());
	wxGetApp().UpdateDarkUI(mod_colour->GetPickerCtrl(), true);
	mod_colour->Bind(wxEVT_COLOURPICKER_CHANGED, ([mod_colour, mod_label](wxCommandEvent e)
	{
		mod_label->SetForegroundColour(mod_colour->GetColour());
		mod_label->Refresh();
	}));
	grid_sizer->Add(0, -1, wxALIGN_CENTRE_VERTICAL);
	grid_sizer->Add(mod_colour, -1, wxALIGN_CENTRE_VERTICAL);
	grid_sizer->Add(mod_label, -1, wxALIGN_CENTRE_VERTICAL | wxEXPAND);
	

	auto buttons = CreateStdDialogButtonSizer(wxOK|wxCANCEL);
	main_sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	wxButton* btn = static_cast<wxButton*>(FindWindowById(wxID_OK, this));
	wxGetApp().UpdateDarkUI(btn);
	wxGetApp().UpdateDarkUI(static_cast<wxButton*>(this->FindWindowById(wxID_CANCEL, this)));
	btn->Bind(wxEVT_BUTTON, [sys_colour, mod_colour, this](wxCommandEvent&) {
		wxGetApp().set_label_clr_sys(sys_colour->GetColour());
		wxGetApp().set_label_clr_modified(mod_colour->GetColour());
		EndModal(wxID_OK);
		});

	SetSizer(main_sizer);
	main_sizer->SetSizeHints(this);
}

} // GUI
} // Slic3r

