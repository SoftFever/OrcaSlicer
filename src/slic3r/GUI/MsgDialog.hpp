#ifndef slic3r_MsgDialog_hpp_
#define slic3r_MsgDialog_hpp_

#include <string>
#include <unordered_map>

#include <wx/dialog.h>
#include <wx/font.h>
#include <wx/bitmap.h>
#include <wx/msgdlg.h>

class wxBoxSizer;
class wxCheckBox;
class wxStaticBitmap;

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
	virtual ~MsgDialog() = default;

	// TODO: refactor with CreateStdDialogButtonSizer usage

protected:
	enum {
		CONTENT_WIDTH = 70,//50,
		CONTENT_MAX_HEIGHT = 60,
		BORDER = 30,
		VERT_SPACING = 15,
		HORIZ_SPACING = 5,
	};

	// button_id is an id of a button that can be added by default, use wxID_NONE to disable
	MsgDialog(wxWindow *parent, const wxString &title, const wxString &headline, wxWindowID button_id = wxID_OK, wxBitmap bitmap = wxNullBitmap);

	void add_btn(wxWindowID btn_id, bool set_focus = false);

	wxFont boldfont;
	wxBoxSizer *content_sizer;
	wxBoxSizer *btn_sizer;
	wxStaticBitmap *logo;
};


// Generic error dialog, used for displaying exceptions
class ErrorDialog : public MsgDialog
{
public:
	// If monospaced_font is true, the error message is displayed using html <code><pre></pre></code> tags,
	// so that the code formatting will be preserved. This is useful for reporting errors from the placeholder parser.
	ErrorDialog(wxWindow *parent, const wxString &msg, bool courier_font);
	ErrorDialog(ErrorDialog &&) = delete;
	ErrorDialog(const ErrorDialog &) = delete;
	ErrorDialog &operator=(ErrorDialog &&) = delete;
	ErrorDialog &operator=(const ErrorDialog &) = delete;
	virtual ~ErrorDialog() = default;

private:
	wxString msg;
};


// Generic warning dialog, used for displaying exceptions
class WarningDialog : public MsgDialog
{
public:
	WarningDialog(	wxWindow *parent,
		            const wxString& message,
		            const wxString& caption = wxEmptyString,
		            long style = wxOK);
	WarningDialog(WarningDialog&&) = delete;
	WarningDialog(const WarningDialog&) = delete;
	WarningDialog &operator=(WarningDialog&&) = delete;
	WarningDialog &operator=(const WarningDialog&) = delete;
	virtual ~WarningDialog() = default;
};

#ifdef _WIN32
// Generic message dialog, used intead of wxMessageDialog
class MessageDialog : public MsgDialog
{
public:
	MessageDialog(	wxWindow *parent,
		            const wxString& message,
		            const wxString& caption = wxEmptyString,
		            long style = wxOK);
	MessageDialog(MessageDialog&&) = delete;
	MessageDialog(const MessageDialog&) = delete;
	MessageDialog &operator=(MessageDialog&&) = delete;
	MessageDialog &operator=(const MessageDialog&) = delete;
	virtual ~MessageDialog() = default;
};
#else
// just a wrapper to wxMessageBox to use the same code on all platforms
class MessageDialog : public wxMessageDialog
{
public:
	MessageDialog(wxWindow* parent,
		const wxString& message,
		const wxString& caption = wxEmptyString,
		long style = wxOK)
    : wxMessageDialog(parent, message, caption, style) {}
	~MessageDialog() {}
};
#endif


}
}

#endif
