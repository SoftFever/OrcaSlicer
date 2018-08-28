#include "AppController.hpp"

#include <thread>
#include <future>

#include <slic3r/GUI/GUI.hpp>
#include <slic3r/GUI/PngExportDialog.hpp>

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
        public IProgressIndicator, public wxEvtHandler {

    wxProgressDialog gauge_;
    using Base = IProgressIndicator;
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
        IProgressIndicator::cancel();
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

// A wrapper progress indicator class around the statusbar created in perl.
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
        if( st <= IProgressIndicator::max() ) {
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
            IProgressIndicator::max(val);
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

};
}

void AppController::set_global_progress_indicator(
        unsigned gid,
        unsigned sid)
{
    wxGauge* gauge = dynamic_cast<wxGauge*>(wxWindow::FindWindowById(gid));
    wxStatusBar* sb = dynamic_cast<wxStatusBar*>(wxWindow::FindWindowById(sid));

    if(gauge && sb) {
        global_progress_indicator(std::make_shared<Wrapper>(gauge, sb, *this));
    }
}

PrintController::PngExportData PrintController::query_png_export_data()
{

    // Implement the logic of the PngExportDialog
    class PngExportView: public PngExportDialog {
        double ratio_, bs_ratio_;
        PrintController& ctl_;
    public:

        PngExportView(PrintController& ctl):
            PngExportDialog(wxTheApp->GetTopWindow()), ctl_(ctl)
        {
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
            ret.exp_time_s = exptime_spin_->GetValue();
            ret.exp_time_first_s = exptime_first_spin_->GetValue();
            ret.corr_x = corr_spin_x_->GetValue();
            ret.corr_y = corr_spin_y_->GetValue();
            ret.corr_z = corr_spin_z_->GetValue();
            return ret;
        }

        void prefill(const PngExportData& data) {
            filepick_ctl_->SetPath(data.zippath);
            spin_reso_width_->SetValue(data.width_px);
            spin_reso_height_->SetValue(data.height_px);
            bed_width_spin_->SetValue(data.width_mm);
            bed_height_spin_->SetValue(data.height_mm);
            exptime_spin_->SetValue(data.exp_time_s);
            exptime_first_spin_->SetValue(data.exp_time_first_s);
            corr_spin_x_->SetValue(data.corr_x);
            corr_spin_y_->SetValue(data.corr_y);
            corr_spin_z_->SetValue(data.corr_z);
            if(data.zippath.empty()) export_btn_->Disable();
            else export_btn_->Enable();
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
                    spin_reso_height_->Update();
                } else {
                    spin_reso_width_->SetValue(
                            std::round(spin_reso_height_->GetValue()*ratio_));
                    spin_reso_width_->Update();
                }
            }
        }

        virtual void EvalBedSpin( wxCommandEvent& event ) override {
            if(bedsize_lock_btn_->GetValue()) {
                if(event.GetId() == bed_width_spin_->GetId()) {
                    bed_height_spin_->SetValue(
                            std::round(bed_width_spin_->GetValue()/bs_ratio_));
                    bed_height_spin_->Update();
                } else {
                    bed_width_spin_->SetValue(
                            std::round(bed_height_spin_->GetValue()*bs_ratio_));
                    bed_width_spin_->Update();
                }
            }
        }

        virtual void onFileChanged( wxFileDirPickerEvent& event ) {
            if(filepick_ctl_->GetPath().IsEmpty()) export_btn_->Disable();
            else export_btn_->Enable();
        }

        virtual void Close( wxCommandEvent& /*event*/ ) {
            auto ret = wxID_OK;

            if(wxFileName(filepick_ctl_->GetPath()).Exists())
                if(!ctl_.report_issue(PrintController::IssueType::WARN_Q,
                                  _(L("File already exists. Overwrite?")),
                                  _(L("Warning")))) ret = wxID_CANCEL;
            EndModal(ret);
        }
    };

    PngExportView exdlg(*this);

    exdlg.prefill(prev_expdata_);

    auto r = exdlg.ShowModal();

    auto ret = exdlg.export_data();
    prev_expdata_ = ret;

    if(r != wxID_OK) ret.zippath.clear();

    return ret;
}
}
