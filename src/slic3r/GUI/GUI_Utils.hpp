#ifndef slic3r_GUI_Utils_hpp_
#define slic3r_GUI_Utils_hpp_

#include <memory>
#include <string>
#include <ostream>

#include <boost/optional.hpp>

#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/debug.h>

class wxCheckBox;
class wxTopLevelWindow;
class wxRect;


namespace Slic3r {
namespace GUI {


wxTopLevelWindow* find_toplevel_parent(wxWindow *window);


class EventGuard
{
    // This is a RAII-style smart-ptr-like guard that will bind any event to any event handler
    // and unbind it as soon as it goes out of scope or unbind() is called.
    // This can be used to solve the annoying problem of wx events being delivered to freed objects.

private:
    // This is a way to type-erase both the event type as well as the handler:

    struct EventStorageBase {
        virtual ~EventStorageBase() {}
    };

    template<class EvTag, class Fun>
    struct EventStorageFun : EventStorageBase {
        wxEvtHandler *emitter;
        EvTag tag;
        Fun fun;

        EventStorageFun(wxEvtHandler *emitter, const EvTag &tag, Fun fun)
            : emitter(emitter)
            , tag(tag)
            , fun(std::move(fun))
        {
            emitter->Bind(this->tag, this->fun);
        }

        virtual ~EventStorageFun() { emitter->Unbind(tag, fun); }
    };

    template<typename EvTag, typename Class, typename EvArg, typename EvHandler>
    struct EventStorageMethod : EventStorageBase {
        typedef void(Class::* MethodPtr)(EvArg &);

        wxEvtHandler *emitter;
        EvTag tag;
        MethodPtr method;
        EvHandler *handler;

        EventStorageMethod(wxEvtHandler *emitter, const EvTag &tag, MethodPtr method, EvHandler *handler)
            : emitter(emitter)
            , tag(tag)
            , method(method)
            , handler(handler)
        {
            emitter->Bind(tag, method, handler);
        }

        virtual ~EventStorageMethod() { emitter->Unbind(tag, method, handler); }
    };

    std::unique_ptr<EventStorageBase> event_storage;
public:
    EventGuard() {}
    EventGuard(const EventGuard&) = delete;
    EventGuard(EventGuard &&other) : event_storage(std::move(other.event_storage)) {}

    template<class EvTag, class Fun>
    EventGuard(wxEvtHandler *emitter, const EvTag &tag, Fun fun)
        :event_storage(new EventStorageFun<EvTag, Fun>(emitter, tag, std::move(fun)))
    {}

    template<typename EvTag, typename Class, typename EvArg, typename EvHandler>
    EventGuard(wxEvtHandler *emitter, const EvTag &tag, void(Class::* method)(EvArg &), EvHandler *handler)
        :event_storage(new EventStorageMethod<EvTag, Class, EvArg, EvHandler>(emitter, tag, method, handler))
    {}

    EventGuard& operator=(const EventGuard&) = delete;
    EventGuard& operator=(EventGuard &&other)
    {
        event_storage = std::move(other.event_storage);
        return *this;
    }

    void unbind() { event_storage.reset(nullptr); }
    explicit operator bool() const noexcept { return !!event_storage; }
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
    std::string serialize() const;
};

std::ostream& operator<<(std::ostream &os, const WindowMetrics& metrics);


}}

#endif
