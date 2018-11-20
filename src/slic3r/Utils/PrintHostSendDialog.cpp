#include "PrintHostSendDialog.hpp"

#include <wx/frame.h>
#include <wx/event.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/MsgDialog.hpp"


namespace fs = boost::filesystem;

namespace Slic3r {

PrintHostSendDialog::PrintHostSendDialog(const fs::path &path, bool can_start_print) :
	MsgDialog(nullptr, _(L("Send G-Code to printer host")), _(L("Upload to Printer Host with the following filename:")), wxID_NONE),
	txt_filename(new wxTextCtrl(this, wxID_ANY, path.filename().wstring())),
	box_print(new wxCheckBox(this, wxID_ANY, _(L("Start printing after upload")))),
	can_start_print(can_start_print)
{
	auto *label_dir_hint = new wxStaticText(this, wxID_ANY, _(L("Use forward slashes ( / ) as a directory separator if needed.")));
	label_dir_hint->Wrap(CONTENT_WIDTH);

	content_sizer->Add(txt_filename, 0, wxEXPAND);
	content_sizer->Add(label_dir_hint);
	content_sizer->AddSpacer(VERT_SPACING);
	content_sizer->Add(box_print, 0, wxBOTTOM, 2*VERT_SPACING);

	btn_sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL));

	txt_filename->SetFocus();
	wxString stem(path.stem().wstring());
	txt_filename->SetSelection(0, stem.Length());

	box_print->Enable(can_start_print);

	Fit();
}

fs::path PrintHostSendDialog::filename() const 
{
	return fs::path(txt_filename->GetValue().wx_str());
}

bool PrintHostSendDialog::print() const 
{ 
	return box_print->GetValue(); }
}
