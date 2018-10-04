#ifndef slic3r_GUI_Utils_hpp_
#define slic3r_GUI_Utils_hpp_

#include <functional>

#include <wx/filedlg.h>

class wxCheckBox;


namespace Slic3r {
namespace GUI {


class CheckboxFileDialog : public wxFileDialog
{
public:
    CheckboxFileDialog(wxWindow *parent,
        const wxString &checkbox_label,
        bool checkbox_value,
        const wxString &message = wxFileSelectorPromptStr,
        const wxString &default_dir = wxEmptyString,
        const wxString &default_file = wxEmptyString,
        const wxString &wildcard = wxFileSelectorDefaultWildcardStr,
        long style = wxFD_DEFAULT_STYLE,
        const wxPoint &pos = wxDefaultPosition,
        const wxSize &size = wxDefaultSize,
        const wxString &name = wxFileDialogNameStr
    );

    bool get_checkbox_value() const;

private:
    std::function<wxWindow*(wxWindow*)> extra_control_creator;
    wxCheckBox *cbox;
};


}}

#endif
