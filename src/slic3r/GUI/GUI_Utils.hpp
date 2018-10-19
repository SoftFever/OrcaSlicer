#ifndef slic3r_GUI_Utils_hpp_
#define slic3r_GUI_Utils_hpp_

#include <functional>
#include <string>

#include <boost/optional.hpp>

#include <wx/filedlg.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>

class wxCheckBox;
class wxTopLevelWindow;
class wxRect;


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
    struct ExtraPanel : public wxPanel
    {
        wxCheckBox *cbox;

        ExtraPanel(wxWindow *parent);
        static wxWindow* ctor(wxWindow *parent);
    };

    wxString checkbox_label;
};


class WindowMetrics
{
private:
    wxRect rect;
    bool maximized;

    WindowMetrics() : maximized(false) {}
public:
    static WindowMetrics from_window(wxTopLevelWindow *window);
    static boost::optional<WindowMetrics> deserialize(const std::string &str);

    wxRect get_rect() const { return rect; }
    bool get_maximized() const { return maximized; }

    void sanitize_for_display(const wxRect &screen_rect);
    std::string serialize();
};


}}

#endif
