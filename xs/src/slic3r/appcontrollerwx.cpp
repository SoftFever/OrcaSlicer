#include "AppController.hpp"

#include <slic3r/GUI/GUI.hpp>

#include <wx/app.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/gauge.h>
#include <wx/statusbr.h>
#include <wx/event.h>

namespace Slic3r {

AppControllerBoilerplate::PathList
AppControllerBoilerplate::query_destination_paths(
        const std::string &title,
        const std::string &extensions) const
{

    wxFileDialog dlg(wxTheApp->GetTopWindow(), wxString(title) );
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
    wxFileDialog dlg(wxTheApp->GetTopWindow(), title );
    dlg.SetWildcard(extensions);

    dlg.SetFilename(hint);

    Path ret;

    if(dlg.ShowModal() == wxID_OK) {
        ret = Path(dlg.GetPath());
    }

    return ret;
}

void AppControllerBoilerplate::report_issue(IssueType issuetype,
                                 const std::string &description,
                                 const std::string &brief)
{
    auto icon = wxICON_INFORMATION;
    switch(issuetype) {
    case IssueType::INFO:   break;
    case IssueType::WARN:   icon = wxICON_WARNING; break;
    case IssueType::ERR:
    case IssueType::FATAL:  icon = wxICON_ERROR;
    }

    wxString str = _("Proba szoveg");
    wxMessageBox(str + _(description), _(brief), icon);
}

wxDEFINE_EVENT(PROGRESS_STATUS_UPDATE_EVENT, wxCommandEvent);

namespace  {
class GuiProgressIndicator:
        public IProgressIndicator, public wxEvtHandler {

    std::shared_ptr<wxProgressDialog> gauge_;
    using Base = IProgressIndicator;
    wxString message_;
    int range_; wxString title_;
    unsigned prc_ = 0;
    bool is_asynch_ = false;

    // status update handler
    void _state( wxCommandEvent& evt) {
        unsigned st = evt.GetInt();
        _state(st);
    }

    void _state( unsigned st) {
        if(st < max()) {
            if(!gauge_) gauge_ = std::make_shared<wxProgressDialog>(
                    title_, message_, range_, wxTheApp->GetTopWindow(),
                        wxPD_APP_MODAL | wxPD_AUTO_HIDE
            );

            if(!gauge_->IsShown()) gauge_->ShowModal();
            Base::state(st);
            gauge_->Update(static_cast<int>(st), message_);
        }

        if(st == max()) {
            prc_++;
            if(prc_ == Base::procedure_count())  {
                gauge_.reset();
                prc_ = 0;
            }
        }
    }

public:

    inline void asynch(bool is) { is_asynch_ = is; }
    inline bool asynch() const { return is_asynch_; }

    inline GuiProgressIndicator(int range, const std::string& title,
                                const std::string& firstmsg) :
        range_(range), message_(_(firstmsg)), title_(_(title))
    {
        Base::max(static_cast<float>(range));
        Base::states(static_cast<unsigned>(range));

        Bind(PROGRESS_STATUS_UPDATE_EVENT,
             &GuiProgressIndicator::_state,
             this);
    }

    virtual void state(float val) override {
        if( val >= 1.0) state(static_cast<unsigned>(val));
    }

    virtual void state(unsigned st) override {
        // send status update event
        if(is_asynch_) {
            auto evt = new wxCommandEvent(PROGRESS_STATUS_UPDATE_EVENT);
            evt->SetInt(st);
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
        unsigned statenum, const std::string& title,
        const std::string& firstmsg) const
{
    auto pri =
            std::make_shared<GuiProgressIndicator>(statenum, title, firstmsg);

    pri->asynch(!is_main_thread());

    return pri;
}

//AppControllerBoilerplate::ProgresIndicatorPtr
//AppControllerBoilerplate::create_progress_indicator(unsigned statenum,
//                                       const std::string& title,
//                                       const std::string& firstmsg) const
//{
//    class GuiProgressIndicator: public IProgressIndicator, public wxEvtHandler {
//        using Base = IProgressIndicator;
//        wxString message_, title_;
//        wxProgressDialog gauge_;

//        void _state( wxCommandEvent& evt) {
//            unsigned st = evt.GetInt();

//            if(title_.compare(gauge_.GetTitle()))
//                    gauge_.SetTitle(title_);

//            if(!gauge_.IsShown()) gauge_.ShowModal();
//            Base::state(st);
//            gauge_.Update(static_cast<int>(st), message_);

//        }
//    public:

//        inline GuiProgressIndicator(int range, const std::string& title,
//                                    const std::string& firstmsg):
//            message_(_(firstmsg)), title_(_(title)),
//            gauge_(title_, message_, range, wxTheApp->GetTopWindow())
//        {
//            gauge_.Show(false);

//            Base::max(static_cast<float>(range));
//            Base::states(static_cast<unsigned>(range));

//            Bind(PROGRESS_STATUS_UPDATE_EVENT,
//                 &GuiProgressIndicator::_state,
//                 this);
//        }

//        virtual void state(float val) override {
//            if( val >= 1.0) state(static_cast<unsigned>(val));
//        }

//        virtual void state(unsigned st) override {
//            // send status update event
//            auto evt = new wxCommandEvent(PROGRESS_STATUS_UPDATE_EVENT);
//            evt->SetInt(st);
//            wxQueueEvent(this, evt);
//        }

//        virtual void message(const std::string & msg) override {
//            message_ = _(msg);
//        }

//        virtual void message_fmt(const std::string& fmt, ...) override {
//            va_list arglist;
//            va_start(arglist, fmt);
//            message_ = wxString::Format(_(fmt), arglist);
//            va_end(arglist);
//        }

//        virtual void title(const std::string & title) override {
//            title_ = _(title);
//        }
//    };

//    auto pri =
//            std::make_shared<GuiProgressIndicator>(statenum, title, firstmsg);

//    return pri;
//}

void AppController::set_global_progress_indicator_id(
        unsigned gid,
        unsigned sid)
{

    class Wrapper: public IProgressIndicator {
        wxGauge *gauge_;
        wxStatusBar *stbar_;
        using Base = IProgressIndicator;
        std::string message_;

        void showProgress(bool show = true) {
            gauge_->Show(show);
            gauge_->Pulse();
        }

    public:

        inline Wrapper(wxGauge *gauge, wxStatusBar *stbar):
            gauge_(gauge), stbar_(stbar)
        {
            Base::max(static_cast<float>(gauge->GetRange()));
            Base::states(static_cast<unsigned>(gauge->GetRange()));
        }

        virtual void state(float val) override {
            if( val <= max() && val >= 1.0) {
                Base::state(val);
                stbar_->SetStatusText(message_);
                gauge_->SetValue(static_cast<int>(val));
            }
        }

        virtual void state(unsigned st) override {
            if( st <= max() ) {
                Base::state(st);

                if(!gauge_->IsShown()) showProgress(true);

                stbar_->SetStatusText(message_);
                if(st == gauge_->GetRange()) {
                    gauge_->SetValue(0);
                    showProgress(false);
                } else {
                    gauge_->SetValue(static_cast<int>(st));
                }
            }
        }

        virtual void message(const std::string & msg) override {
            message_ = msg;
        }

        virtual void message_fmt(const std::string& fmt, ...) override {
            va_list arglist;
            va_start(arglist, fmt);
            message_ = wxString::Format(_(fmt), arglist);
            va_end(arglist);
        }

        virtual void title(const std::string & /*title*/) override {}

    };

    wxGauge* gauge = dynamic_cast<wxGauge*>(wxWindow::FindWindowById(gid));
    wxStatusBar* sb = dynamic_cast<wxStatusBar*>(wxWindow::FindWindowById(sid));

    if(gauge && sb) {
        global_progressind_ = std::make_shared<Wrapper>(gauge, sb);
    }
}

}
