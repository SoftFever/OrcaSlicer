#ifndef slic3r_MsgDialog_hpp_
#define slic3r_MsgDialog_hpp_

#include <string>
#include <unordered_map>

#include <wx/dialog.h>
#include <wx/font.h>
#include <wx/bitmap.h>

#include "slic3r/Utils/Semver.hpp"

class wxBoxSizer;
class wxCheckBox;

namespace Slic3r {

namespace GUI {


// A message / query dialog with a bitmap on the left and any content on the right
// with buttons underneath.
struct MsgDialog : wxDialog
{
	MsgDialog(MsgDialog &&) = delete;
	MsgDialog(const MsgDialog &) = delete;
	MsgDialog &operator=(MsgDialog &&) = delete;
	MsgDialog &operator=(const MsgDialog &) = delete;
	virtual ~MsgDialog();

	// TODO: refactor with CreateStdDialogButtonSizer usage

protected:
	enum {
		CONTENT_WIDTH = 500,
		CONTENT_MAX_HEIGHT = 600,
		BORDER = 30,
		VERT_SPACING = 15,
		HORIZ_SPACING = 5,
	};

	// button_id is an id of a button that can be added by default, use wxID_NONE to disable
	MsgDialog(wxWindow *parent, const wxString &title, const wxString &headline, wxWindowID button_id = wxID_OK);
	MsgDialog(wxWindow *parent, const wxString &title, const wxString &headline, wxBitmap bitmap, wxWindowID button_id = wxID_OK);

	wxFont boldfont;
	wxBoxSizer *content_sizer;
	wxBoxSizer *btn_sizer;
};


// Generic error dialog, used for displaying exceptions
struct ErrorDialog : MsgDialog
{
	ErrorDialog(wxWindow *parent, const wxString &msg);
	ErrorDialog(ErrorDialog &&) = delete;
	ErrorDialog(const ErrorDialog &) = delete;
	ErrorDialog &operator=(ErrorDialog &&) = delete;
	ErrorDialog &operator=(const ErrorDialog &) = delete;
	virtual ~ErrorDialog();
};


}
}

#endif
