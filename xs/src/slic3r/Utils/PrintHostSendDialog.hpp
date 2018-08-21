#ifndef slic3r_PrintHostSendDialog_hpp_
#define slic3r_PrintHostSendDialog_hpp_

#include <string>

#include <boost/filesystem/path.hpp>

#include <wx/string.h>
#include <wx/frame.h>
#include <wx/event.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/MsgDialog.hpp"


namespace Slic3r {

class PrintHostSendDialog : public GUI::MsgDialog
{
private:
	wxTextCtrl *txt_filename;
	wxCheckBox *box_print;
	bool can_start_print;

public:
	PrintHostSendDialog(const boost::filesystem::path &path, bool can_start_print);
	boost::filesystem::path filename() const;
	bool print() const;
};

}

#endif
