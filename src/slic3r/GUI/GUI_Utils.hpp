#ifndef slic3r_GUI_Utils_hpp_
#define slic3r_GUI_Utils_hpp_

#include <functional>
#include <string>

#include <boost/optional.hpp>

#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>

class wxCheckBox;
class wxTopLevelWindow;
class wxRect;


namespace Slic3r {
namespace GUI {


wxTopLevelWindow* find_toplevel_parent(wxWindow *window);


class EventGuard
{
public:
    EventGuard() {}
    EventGuard(const EventGuard&) = delete;
    EventGuard(EventGuard &&other) : unbinder(std::move(other.unbinder)) {}

    ~EventGuard() {
        if (unbinder) {
            unbinder(false);
        }
    }

    template<class EvTag, class Fun> void bind(wxEvtHandler *emitter, const EvTag &type, Fun fun)
    {
        // This is a way to type-erase both the event type as well as the handler:

        unbinder = std::move([=](bool bind) {
            if (bind) {
                emitter->Bind(type, fun);
            } else {
                emitter->Unbind(type, fun);
            }
        });

        unbinder(true);
    }

    EventGuard& operator=(const EventGuard&) = delete;
    EventGuard& operator=(EventGuard &&other)
    {
        unbinder.swap(other.unbinder);
        return *this;
    }
private:
    std::function<void(bool)> unbinder;
};


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
