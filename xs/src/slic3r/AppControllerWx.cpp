#include "AppController.hpp"

#include <thread>

#include <slic3r/GUI/GUI.hpp>
#include <slic3r/GUI/PngExportDialog.hpp>

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

    wxMessageBox(description, brief, icon);
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

    const int id_ = wxWindow::NewControlId();

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
             this, id_);
    }

    virtual void state(float val) override {
        if( val >= 1.0) state(static_cast<unsigned>(val));
    }

    void state(unsigned st) {
        // send status update event
        if(is_asynch_) {
            auto evt = new wxCommandEvent(PROGRESS_STATUS_UPDATE_EVENT, id_);
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

namespace {
class Wrapper: public IProgressIndicator, public wxEvtHandler {
    wxGauge *gauge_;
    wxStatusBar *stbar_;
    using Base = IProgressIndicator;
    std::string message_;
    AppControllerBoilerplate& ctl_;

    void showProgress(bool show = true) {
        gauge_->Show(show);
    }

    void _state(unsigned st) {
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
        if(val >= 1.0) state(unsigned(val));
    }

    void state(unsigned st) {
        if(!ctl_.is_main_thread()) {
            auto evt = new wxCommandEvent(PROGRESS_STATUS_UPDATE_EVENT, id_);
            evt->SetInt(st);
            wxQueueEvent(this, evt);
        } else _state(st);
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

PrintController::PngExportData PrintController::query_png_export_data()
{

    class PngExportView: public PngExportDialog {
        double ratio_, bs_ratio_;
    public:

        PngExportView(): PngExportDialog(wxTheApp->GetTopWindow()) {
            ratio_ = double(spin_reso_width_->GetValue()) /
                    spin_reso_height_->GetValue();

            bs_ratio_ = bed_width_spin_->GetValue() /
                    bed_height_spin_->GetValue();
        }

        PngExportData export_data() const {
            PrintController::PngExportData ret;
            ret.zippath = filepick_ctl_->GetPath();
            ret.width_px = spin_reso_width_->GetValue();
            ret.height_px = spin_reso_height_->GetValue();
            ret.width_mm = bed_width_spin_->GetValue();
            ret.height_mm = bed_height_spin_->GetValue();
            ret.corr = corr_spin_->GetValue();
            return ret;
        }

        void prefill(const PngExportData& data) {
            filepick_ctl_->SetPath(data.zippath);
            spin_reso_width_->SetValue(data.width_px);
            spin_reso_height_->SetValue(data.height_px);
            bed_width_spin_->SetValue(data.width_mm);
            bed_height_spin_->SetValue(data.height_mm);
            corr_spin_->SetValue(data.corr);
        }

        virtual void ResoLock( wxCommandEvent& /*event*/ ) override {
            ratio_ = double(spin_reso_width_->GetValue()) /
                    double(spin_reso_height_->GetValue());
        }

        virtual void BedsizeLock( wxCommandEvent& /*event*/ ) override {
            bs_ratio_ = bed_width_spin_->GetValue() /
                    bed_height_spin_->GetValue();
        }

        virtual void EvalResoSpin( wxCommandEvent& event ) override {
            if(reso_lock_btn_->GetValue()) {

                if(event.GetId() == spin_reso_width_->GetId()) {
                    spin_reso_height_->SetValue(
                            std::round(spin_reso_width_->GetValue()/ratio_));
                } else {
                    spin_reso_width_->SetValue(
                            std::round(spin_reso_height_->GetValue()*ratio_));
                }
            }
        }

        virtual void EvalBedSpin( wxCommandEvent& event ) override {
            if(bedsize_lock_btn_->GetValue()) {

                if(event.GetId() == bed_width_spin_->GetId()) {
                    bed_height_spin_->SetValue(
                            std::round(bed_width_spin_->GetValue()/bs_ratio_));
                } else {
                    bed_width_spin_->SetValue(
                            std::round(bed_height_spin_->GetValue()*bs_ratio_));
                }
            }
        }
    };

    PngExportView exdlg;

    exdlg.prefill(prev_expdata_);

    auto r = exdlg.ShowModal();

    auto ret = exdlg.export_data();
    prev_expdata_ = ret;

    if(r != wxID_OK) ret.zippath.clear();

    return ret;
}

}
