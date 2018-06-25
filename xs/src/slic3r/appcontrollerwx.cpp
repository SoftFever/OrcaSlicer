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
    dlg.GetFilenames(paths);

    PathList ret(paths.size(), "");
    for(auto& p : paths) ret.push_back(p.ToStdString());

    return ret;
}

AppControllerBoilerplate::Path
AppControllerBoilerplate::query_destination_path(
        const std::string &title,
        const std::string &extensions) const
{
    wxFileDialog dlg(wxTheApp->GetTopWindow(), title );
    dlg.SetWildcard(extensions);

    dlg.ShowModal();

    Path ret(dlg.GetFilename());

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

AppControllerBoilerplate::ProgresIndicatorPtr
AppControllerBoilerplate::createProgressIndicator(unsigned statenum,
                                       const std::string& title,
                                       const std::string& firstmsg) const
{
    class GuiProgressIndicator:
            public ProgressIndicator, public wxEvtHandler {

        std::shared_ptr<wxProgressDialog> gauge_;
        using Base = ProgressIndicator;
        wxString message_;
        int range_; wxString title_;

        // status update handler
        void _state( wxCommandEvent& evt) {
            unsigned st = evt.GetInt();
            if( st < max() ) {

                if(!gauge_) gauge_ = std::make_shared<wxProgressDialog>(
                        title_, message_, range_, wxTheApp->GetTopWindow()
                );

                if(!gauge_->IsShown()) gauge_->ShowModal();
                Base::state(st);
                gauge_->Update(static_cast<int>(st), message_);

            } else {
                gauge_.reset();
            }
        }

    public:

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
            auto evt = new wxCommandEvent(PROGRESS_STATUS_UPDATE_EVENT);
            evt->SetInt(st);
            wxQueueEvent(this, evt);
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

    auto pri =
            std::make_shared<GuiProgressIndicator>(statenum, title, firstmsg);

    return pri;
}

void AppController::set_global_progress_indicator_id(
        unsigned gid,
        unsigned sid)
{

    class Wrapper: public ProgressIndicator {
        wxGauge *gauge_;
        wxStatusBar *stbar_;
        using Base = ProgressIndicator;
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

        virtual void messageFmt(const std::string& fmt, ...) {
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
        auto&& progind = std::make_shared<Wrapper>(gauge, sb);
        progressIndicator(progind);
        if(printctl) printctl->progressIndicator(progind);
    }
}

}
