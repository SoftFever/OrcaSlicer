#include "AppController.hpp"

#include <wx/stdstream.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

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

bool AppControllerGui::supports_asynch() const
{
    return true;
}

void AppControllerGui::process_events()
{
    wxYieldIfNeeded();
}

FilePathList AppControllerGui::query_destination_paths(
        const std::string &title,
        const std::string &extensions,
        const std::string &/*functionid*/,
        const std::string& hint) const
{

    wxFileDialog dlg(wxTheApp->GetTopWindow(), _(title) );
    dlg.SetWildcard(extensions);

    dlg.SetFilename(hint);

    FilePathList ret;

    if(dlg.ShowModal() == wxID_OK) {
        wxArrayString paths;
        dlg.GetPaths(paths);
        for(auto& p : paths) ret.push_back(p.ToStdString());
    }

    return ret;
}

FilePath AppControllerGui::query_destination_path(
        const std::string &title,
        const std::string &extensions,
        const std::string &/*functionid*/,
        const std::string& hint) const
{
    wxFileDialog dlg(wxTheApp->GetTopWindow(), _(title) );
    dlg.SetWildcard(extensions);

    dlg.SetFilename(hint);

    FilePath ret;

    if(dlg.ShowModal() == wxID_OK) {
        ret = FilePath(dlg.GetPath());
    }

    return ret;
}

bool AppControllerGui::report_issue(IssueType issuetype,
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

wxDEFINE_EVENT(PROGRESS_STATUS_UPDATE_EVENT, wxCommandEvent);

struct Zipper::Impl {
    wxFileName fpath;
    wxFFileOutputStream zipfile;
    wxZipOutputStream zipstream;
    wxStdOutputStream pngstream;

    Impl(const std::string& zipfile_path):
        fpath(zipfile_path),
        zipfile(zipfile_path),
        zipstream(zipfile),
        pngstream(zipstream)
    {
        if(!zipfile.IsOk())
            throw std::runtime_error(L("Cannot create zip file."));
    }
};

Zipper::Zipper(const std::string &zipfilepath)
{
    m_impl.reset(new Impl(zipfilepath));
}

Zipper::~Zipper() {}

void Zipper::next_entry(const std::string &fname)
{
    m_impl->zipstream.PutNextEntry(fname);
}

std::string Zipper::get_name() const
{
    return m_impl->fpath.GetName().ToStdString();
}

std::ostream &Zipper::stream()
{
    return m_impl->pngstream;
}

void Zipper::close()
{
    m_impl->zipstream.Close();
    m_impl->zipfile.Close();
}

namespace  {

/*
 * A simple thread safe progress dialog implementation that can be used from
 * the main thread as well.
 */
class GuiProgressIndicator:
        public ProgressIndicator, public wxEvtHandler {

    wxProgressDialog m_gauge;
    using Base = ProgressIndicator;
    wxString m_message;
    int m_range; wxString m_title;
    bool m_is_asynch = false;

    const int m_id = wxWindow::NewControlId();

    // status update handler
    void _state( wxCommandEvent& evt) {
        unsigned st = evt.GetInt();
        m_message = evt.GetString();
        _state(st);
    }

    // Status update implementation
    void _state( unsigned st) {
        if(!m_gauge.IsShown()) m_gauge.ShowModal();
        Base::state(st);
        if(!m_gauge.Update(static_cast<int>(st), m_message)) {
            cancel();
        }
    }

public:

    /// Setting whether it will be used from the UI thread or some worker thread
    inline void asynch(bool is) { m_is_asynch = is; }

    /// Get the mode of parallel operation.
    inline bool asynch() const { return m_is_asynch; }

    inline GuiProgressIndicator(int range, const wxString& title,
                                const wxString& firstmsg) :
        m_gauge(title, firstmsg, range, wxTheApp->GetTopWindow(),
               wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT),

        m_message(firstmsg),
        m_range(range), m_title(title)
    {
        Base::max(static_cast<float>(range));
        Base::states(static_cast<unsigned>(range));

        Bind(PROGRESS_STATUS_UPDATE_EVENT,
             &GuiProgressIndicator::_state,
             this, m_id);
    }

    virtual void state(float val) override {
        state(static_cast<unsigned>(val));
    }

    void state(unsigned st) {
        // send status update event
        if(m_is_asynch) {
            auto evt = new wxCommandEvent(PROGRESS_STATUS_UPDATE_EVENT, m_id);
            evt->SetInt(st);
            evt->SetString(m_message);
            wxQueueEvent(this, evt);
        } else _state(st);
    }

    virtual void message(const std::string & msg) override {
        m_message = _(msg);
    }

    virtual void messageFmt(const std::string& fmt, ...) {
        va_list arglist;
        va_start(arglist, fmt);
        m_message = wxString::Format(_(fmt), arglist);
        va_end(arglist);
    }

    virtual void title(const std::string & title) override {
        m_title = _(title);
    }
};
}

ProgresIndicatorPtr AppControllerGui::create_progress_indicator(
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

namespace {

class Wrapper: public ProgressIndicator, public wxEvtHandler {
    ProgressStatusBar *m_sbar;
    using Base = ProgressIndicator;
    wxString m_message;
    AppControllerBase& m_ctl;

    void showProgress(bool show = true) {
        m_sbar->show_progress(show);
    }

    void _state(unsigned st) {
        if( st <= ProgressIndicator::max() ) {
            Base::state(st);
            m_sbar->set_status_text(m_message);
            m_sbar->set_progress(st);
        }
    }

    // status update handler
    void _state( wxCommandEvent& evt) {
        unsigned st = evt.GetInt(); _state(st);
    }

    const int id_ = wxWindow::NewControlId();

public:

    inline Wrapper(ProgressStatusBar *sbar,
                   AppControllerBase& ctl):
        m_sbar(sbar), m_ctl(ctl)
    {
        Base::max(static_cast<float>(m_sbar->get_range()));
        Base::states(static_cast<unsigned>(m_sbar->get_range()));

        Bind(PROGRESS_STATUS_UPDATE_EVENT,
             &Wrapper::_state,
             this, id_);
    }

    virtual void state(float val) override {
        state(unsigned(val));
    }

    virtual void max(float val) override {
        if(val > 1.0) {
            m_sbar->set_range(static_cast<int>(val));
            ProgressIndicator::max(val);
        }
    }

    void state(unsigned st) {
        if(!m_ctl.is_main_thread()) {
            auto evt = new wxCommandEvent(PROGRESS_STATUS_UPDATE_EVENT, id_);
            evt->SetInt(st);
            wxQueueEvent(this, evt);
        } else {
            _state(st);
        }
    }

    virtual void message(const std::string & msg) override {
        m_message = _(msg);
    }

    virtual void message_fmt(const std::string& fmt, ...) override {
        va_list arglist;
        va_start(arglist, fmt);
        m_message = wxString::Format(_(fmt), arglist);
        va_end(arglist);
    }

    virtual void title(const std::string & /*title*/) override {}

    virtual void on_cancel(CancelFn fn) override {
        m_sbar->set_cancel_callback(fn);
        Base::on_cancel(fn);
    }

};
}

void AppController::set_global_progress_indicator(ProgressStatusBar *prsb)
{
    if(prsb) {
        auto ctl = GUI::get_appctl();
        ctl->global_progress_indicator(std::make_shared<Wrapper>(prsb, *ctl));
    }
}

}
