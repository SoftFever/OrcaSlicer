#include "AppController.hpp"

#include <thread>
#include <future>

#include <slic3r/GUI/GUI.hpp>

#include <wx/app.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/gauge.h>
#include <wx/statusbr.h>
#include <wx/event.h>

// This source file implements the UI dependent methods of the AppControllers.
// It will be clear what is needed to be reimplemented in case of a UI framework
// change or a CLI client creation. In this particular case we use wxWidgets to
// implement everything.

namespace Slic3r {

bool AppControllerBoilerplate::supports_asynch() const
{
    return true;
}

void AppControllerBoilerplate::process_events()
{
    wxSafeYield();
}

AppControllerBoilerplate::PathList
AppControllerBoilerplate::query_destination_paths(
        const std::string &title,
        const std::string &extensions) const
{

    wxFileDialog dlg(wxTheApp->GetTopWindow(), _(title) );
    dlg.SetWildcard(extensions);

    dlg.ShowModal();

    wxArrayString paths;
    dlg.GetPaths(paths);

    PathList ret(paths.size(), "");
    for(auto& p : paths) ret.push_back(p.ToStdString());

    return ret;
}

AppControllerBoilerplate::Path
AppControllerBoilerplate::query_destination_path(
        const std::string &title,
        const std::string &extensions,
        const std::string& hint) const
{
    wxFileDialog dlg(wxTheApp->GetTopWindow(), _(title) );
    dlg.SetWildcard(extensions);

    dlg.SetFilename(hint);

    Path ret;

    if(dlg.ShowModal() == wxID_OK) {
        ret = Path(dlg.GetPath());
    }

    return ret;
}

bool AppControllerBoilerplate::report_issue(IssueType issuetype,
                                 const std::string &description,
                                 const std::string &brief)
{
    auto icon = wxICON_INFORMATION;
    auto style = wxOK|wxCENTRE;
    switch(issuetype) {
    case IssueType::INFO:   break;
    case IssueType::WARN:   icon = wxICON_WARNING; break;
    case IssueType::WARN_Q: icon = wxICON_WARNING; style |= wxCANCEL; break;
    case IssueType::ERR:
    case IssueType::FATAL:  icon = wxICON_ERROR;
    }

    auto ret = wxMessageBox(_(description), _(brief), icon | style);
    return ret != wxCANCEL;
}

bool AppControllerBoilerplate::report_issue(
        AppControllerBoilerplate::IssueType issuetype,
        const std::string &description)
{
    return report_issue(issuetype, description, std::string());
}

wxDEFINE_EVENT(PROGRESS_STATUS_UPDATE_EVENT, wxCommandEvent);

namespace  {

/*
 * A simple thread safe progress dialog implementation that can be used from
 * the main thread as well.
 */
class GuiProgressIndicator:
        public ProgressIndicator, public wxEvtHandler {

    wxProgressDialog gauge_;
    using Base = ProgressIndicator;
    wxString message_;
    int range_; wxString title_;
    bool is_asynch_ = false;

    const int id_ = wxWindow::NewControlId();

    // status update handler
    void _state( wxCommandEvent& evt) {
        unsigned st = evt.GetInt();
        message_ = evt.GetString();
        _state(st);
    }

    // Status update implementation
    void _state( unsigned st) {
        if(!gauge_.IsShown()) gauge_.ShowModal();
        Base::state(st);
        gauge_.Update(static_cast<int>(st), message_);
    }

public:

    /// Setting whether it will be used from the UI thread or some worker thread
    inline void asynch(bool is) { is_asynch_ = is; }

    /// Get the mode of parallel operation.
    inline bool asynch() const { return is_asynch_; }

    inline GuiProgressIndicator(int range, const wxString& title,
                                const wxString& firstmsg) :
        gauge_(title, firstmsg, range, wxTheApp->GetTopWindow(),
               wxPD_APP_MODAL | wxPD_AUTO_HIDE),
        message_(firstmsg),
        range_(range), title_(title)
    {
        Base::max(static_cast<float>(range));
        Base::states(static_cast<unsigned>(range));

        Bind(PROGRESS_STATUS_UPDATE_EVENT,
             &GuiProgressIndicator::_state,
             this, id_);
    }

    virtual void state(float val) override {
        state(static_cast<unsigned>(val));
    }

    void state(unsigned st) {
        // send status update event
        if(is_asynch_) {
            auto evt = new wxCommandEvent(PROGRESS_STATUS_UPDATE_EVENT, id_);
            evt->SetInt(st);
            evt->SetString(message_);
            wxQueueEvent(this, evt);
        } else _state(st);
    }

    virtual void message(const std::string & msg) override {
        message_ = _(msg);
    }

    virtual void messageFmt(const std::string& fmt, ...) {
        va_list arglist;
        va_start(arglist, fmt);
        message_ = wxString::Format(_(fmt), arglist);
        va_end(arglist);
    }

    virtual void title(const std::string & title) override {
        title_ = _(title);
    }
};
}

AppControllerBoilerplate::ProgresIndicatorPtr
AppControllerBoilerplate::create_progress_indicator(
        unsigned statenum,
        const std::string& title,
        const std::string& firstmsg) const
{
    auto pri =
            std::make_shared<GuiProgressIndicator>(statenum, title, firstmsg);

    // We set up the mode of operation depending of the creator thread's
    // identity
    pri->asynch(!is_main_thread());

    return pri;
}

AppControllerBoilerplate::ProgresIndicatorPtr
AppControllerBoilerplate::create_progress_indicator(
        unsigned statenum, const std::string &title) const
{
    return create_progress_indicator(statenum, title, std::string());
}

namespace {

// A wrapper progress indicator class around the statusbar created in perl.
class Wrapper: public ProgressIndicator, public wxEvtHandler {
    wxGauge *gauge_;
    wxStatusBar *stbar_;
    using Base = ProgressIndicator;
    wxString message_;
    AppControllerBoilerplate& ctl_;

    void showProgress(bool show = true) {
        gauge_->Show(show);
    }

    void _state(unsigned st) {
        if( st <= ProgressIndicator::max() ) {
            Base::state(st);

            if(!gauge_->IsShown()) showProgress(true);

            stbar_->SetStatusText(message_);
            if(static_cast<long>(st) == gauge_->GetRange()) {
                gauge_->SetValue(0);
                showProgress(false);
            } else {
                gauge_->SetValue(static_cast<int>(st));
            }
        }
    }

    // status update handler
    void _state( wxCommandEvent& evt) {
        unsigned st = evt.GetInt(); _state(st);
    }

    const int id_ = wxWindow::NewControlId();

public:

    inline Wrapper(wxGauge *gauge, wxStatusBar *stbar,
                   AppControllerBoilerplate& ctl):
        gauge_(gauge), stbar_(stbar), ctl_(ctl)
    {
        Base::max(static_cast<float>(gauge->GetRange()));
        Base::states(static_cast<unsigned>(gauge->GetRange()));

        Bind(PROGRESS_STATUS_UPDATE_EVENT,
             &Wrapper::_state,
             this, id_);
    }

    virtual void state(float val) override {
        state(unsigned(val));
    }

    virtual void max(float val) override {
        if(val > 1.0) {
            gauge_->SetRange(static_cast<int>(val));
            ProgressIndicator::max(val);
        }
    }

    void state(unsigned st) {
        if(!ctl_.is_main_thread()) {
            auto evt = new wxCommandEvent(PROGRESS_STATUS_UPDATE_EVENT, id_);
            evt->SetInt(st);
            wxQueueEvent(this, evt);
        } else {
            _state(st);
        }
    }

    virtual void message(const std::string & msg) override {
        message_ = _(msg);
    }

    virtual void message_fmt(const std::string& fmt, ...) override {
        va_list arglist;
        va_start(arglist, fmt);
        message_ = wxString::Format(_(fmt), arglist);
        va_end(arglist);
    }

    virtual void title(const std::string & /*title*/) override {}

};
}

void AppController::set_global_progress_indicator(
        unsigned gid,
        unsigned sid)
{
    wxGauge* gauge = dynamic_cast<wxGauge*>(wxWindow::FindWindowById(gid));
    wxStatusBar* sb = dynamic_cast<wxStatusBar*>(wxWindow::FindWindowById(sid));

    if(gauge && sb) {
        global_progressind_ = std::make_shared<Wrapper>(gauge, sb, *this);
    }
}
}
