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

void ButtonsDescription::FillSizerWithTextColorDescriptions(wxSizer* sizer, wxWindow* parent, wxColourPickerCtrl** sys_colour, wxColourPickerCtrl** mod_colour)
{
	wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(3, 5, 5);
	sizer->Add(grid_sizer, 0, wxEXPAND);

	ScalableBitmap bmp_delete = ScalableBitmap(parent, "cross");
	ScalableBitmap bmp_delete_focus = ScalableBitmap(parent, "cross_focus");

	auto add_color = [grid_sizer, parent](wxColourPickerCtrl** color_picker, const wxColour& color, const wxColour& def_color, wxString label_text) {
		// wrap the label_text to the max 80 characters
		if (label_text.Len() > 80) {
			size_t brack_pos = label_text.find_last_of(" ", 79);
			if (brack_pos > 0 && brack_pos < 80)
				label_text.insert(brack_pos + 1, "\n");
		}

		auto sys_label = new wxStaticText(parent, wxID_ANY, label_text);
		sys_label->SetForegroundColour(color);

		*color_picker = new wxColourPickerCtrl(parent, wxID_ANY, color);
		wxGetApp().UpdateDarkUI((*color_picker)->GetPickerCtrl(), true);
		(*color_picker)->Bind(wxEVT_COLOURPICKER_CHANGED, [color_picker, sys_label](wxCommandEvent&) {
			sys_label->SetForegroundColour((*color_picker)->GetColour());
			sys_label->Refresh();
		});

		auto btn = new ScalableButton(parent, wxID_ANY, "undo");
		btn->SetToolTip(_L("Revert color to default"));
		btn->Bind(wxEVT_BUTTON, [sys_label, color_picker, def_color](wxEvent& event) {
			(*color_picker)->SetColour(def_color);
			sys_label->SetForegroundColour(def_color);
			sys_label->Refresh();
		});
		parent->Bind(wxEVT_UPDATE_UI, [color_picker, def_color](wxUpdateUIEvent& evt) {
			evt.Enable((*color_picker)->GetColour() != def_color);
	    }, btn->GetId());

		grid_sizer->Add(*color_picker, 0, wxALIGN_CENTRE_VERTICAL);
		grid_sizer->Add(btn, 0, wxALIGN_CENTRE_VERTICAL);
		grid_sizer->Add(sys_label, 0, wxALIGN_CENTRE_VERTICAL | wxEXPAND);
	};

	add_color(sys_colour, wxGetApp().get_label_clr_sys(),	  wxGetApp().get_label_default_clr_system(),	_L("Value is the same as the system value"));
	add_color(mod_colour, wxGetApp().get_label_clr_modified(),wxGetApp().get_label_default_clr_modified(),	_L("Value was changed and is not equal to the system value or the last saved preset"));
}

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
	wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	FillSizerWithTextColorDescriptions(sizer, this, &sys_colour, &mod_colour);
	main_sizer->Add(sizer, 0, wxEXPAND | wxALL, 20);	

	auto buttons = CreateStdDialogButtonSizer(wxOK|wxCANCEL);
	main_sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	wxButton* btn = static_cast<wxButton*>(FindWindowById(wxID_OK, this));
	btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		wxGetApp().set_label_clr_sys(sys_colour->GetColour());
		wxGetApp().set_label_clr_modified(mod_colour->GetColour());
		EndModal(wxID_OK);
		});

	wxGetApp().UpdateDarkUI(btn);
	wxGetApp().UpdateDarkUI(static_cast<wxButton*>(FindWindowById(wxID_CANCEL, this)));

	SetSizer(main_sizer);
	main_sizer->SetSizeHints(this);
}

} // GUI
} // Slic3r

