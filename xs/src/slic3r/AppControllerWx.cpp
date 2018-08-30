#include "AppController.hpp"

#include <thread>
#include <future>

#include <slic3r/GUI/GUI.hpp>
#include <slic3r/GUI/ProgressStatusBar.hpp>

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
        const string &title,
        const std::string &extensions) const
{

    wxFileDialog dlg(wxTheApp->GetTopWindow(), title );
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
        const string &title,
        const std::string &extensions,
        const std::string& hint) const
{
    wxFileDialog dlg(wxTheApp->GetTopWindow(), title );
    dlg.SetWildcard(extensions);

    dlg.SetFilename(hint);

    Path ret;

    if(dlg.ShowModal() == wxID_OK) {
        ret = Path(dlg.GetPath());
    }

    return ret;
}

bool AppControllerBoilerplate::report_issue(IssueType issuetype,
                                 const string &description,
                                 const string &brief)
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

    auto ret = wxMessageBox(description, brief, icon | style);
    return ret != wxCANCEL;
}

bool AppControllerBoilerplate::report_issue(
        AppControllerBoilerplate::IssueType issuetype,
        const string &description)
{
    return report_issue(issuetype, description, string());
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

    inline GuiProgressIndicator(int range, const string& title,
                                const string& firstmsg) :
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

    virtual void cancel() override {
        update(max(), "Abort");
        ProgressIndicator::cancel();
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

    virtual void message(const string & msg) override {
        message_ = msg;
    }

    virtual void messageFmt(const string& fmt, ...) {
        va_list arglist;
        va_start(arglist, fmt);
        message_ = wxString::Format(wxString(fmt), arglist);
        va_end(arglist);
    }

    virtual void title(const string & title) override {
        title_ = title;
    }
};
}

AppControllerBoilerplate::ProgresIndicatorPtr
AppControllerBoilerplate::create_progress_indicator(
        unsigned statenum, const string& title, const string& firstmsg) const
{
    auto pri =
            std::make_shared<GuiProgressIndicator>(statenum, title, firstmsg);

    // We set up the mode of operation depending of the creator thread's
    // identity
    pri->asynch(!is_main_thread());

    return pri;
}

AppControllerBoilerplate::ProgresIndicatorPtr
AppControllerBoilerplate::create_progress_indicator(unsigned statenum,
                                                    const string &title) const
{
    return create_progress_indicator(statenum, title, string());
}

namespace {

class Wrapper: public ProgressIndicator, public wxEvtHandler {
    ProgressStatusBar *sbar_;
    using Base = ProgressIndicator;
    std::string message_;
    AppControllerBoilerplate& ctl_;

    void showProgress(bool show = true) {
        sbar_->show_progress(show);
    }

    void _state(unsigned st) {
        if( st <= ProgressIndicator::max() ) {
            Base::state(st);
            sbar_->set_status_text(message_);
            sbar_->set_progress(st);
        }
    }

    // status update handler
    void _state( wxCommandEvent& evt) {
        unsigned st = evt.GetInt(); _state(st);
    }

    const int id_ = wxWindow::NewControlId();

public:

    inline Wrapper(ProgressStatusBar *sbar,
                   AppControllerBoilerplate& ctl):
        sbar_(sbar), ctl_(ctl)
    {
        Base::max(static_cast<float>(sbar_->get_range()));
        Base::states(static_cast<unsigned>(sbar_->get_range()));

        Bind(PROGRESS_STATUS_UPDATE_EVENT,
             &Wrapper::_state,
             this, id_);
    }

    virtual void state(float val) override {
        state(unsigned(val));
    }

    virtual void max(float val) override {
        if(val > 1.0) {
            sbar_->set_range(static_cast<int>(val));
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

    virtual void message(const string & msg) override {
        message_ = msg;
    }

    virtual void message_fmt(const string& fmt, ...) override {
        va_list arglist;
        va_start(arglist, fmt);
        message_ = wxString::Format(fmt, arglist);
        va_end(arglist);
    }

    virtual void title(const string & /*title*/) override {}

    virtual void on_cancel(CancelFn fn) override {
        sbar_->set_cancel_callback(fn);
        Base::on_cancel(fn);
    }

};
}

void AppController::set_global_progress_indicator(ProgressStatusBar *prsb)
{
    if(prsb) {
        global_progress_indicator(std::make_shared<Wrapper>(prsb, *this));
    }
}
}
