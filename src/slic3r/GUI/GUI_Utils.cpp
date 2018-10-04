#include "GUI_Utils.hpp"

#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/checkbox.h>


namespace Slic3r {
namespace GUI {


CheckboxFileDialog::CheckboxFileDialog(wxWindow *parent,
	const wxString &checkbox_label,
    bool checkbox_value,
    const wxString &message,
    const wxString &default_dir,
    const wxString &default_file,
    const wxString &wildcard,
    long style,
    const wxPoint &pos,
    const wxSize &size,
    const wxString &name
)
    : wxFileDialog(parent, message, default_dir, default_file, wildcard, style, pos, size, name)
    , cbox(nullptr)
{
	if (checkbox_label.IsEmpty()) {
		return;
	}

	extra_control_creator = [this, checkbox_label](wxWindow *parent) -> wxWindow* {
		wxPanel* panel = new wxPanel(parent, -1);
	    wxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
	    this->cbox = new wxCheckBox(panel, wxID_HIGHEST + 1, checkbox_label);
	    this->cbox->SetValue(true);
	    sizer->AddSpacer(5);
	    sizer->Add(this->cbox, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
	    panel->SetSizer(sizer);
	    sizer->SetSizeHints(panel);

	    return panel;
	};

    SetExtraControlCreator(*extra_control_creator.target<ExtraControlCreatorFunction>());
}

bool CheckboxFileDialog::get_checkbox_value() const
{
	return this->cbox != nullptr ? cbox->IsChecked() : false;
}


}
}
