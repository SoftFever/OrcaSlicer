#include "AppController.hpp"

#include <slic3r/GUI/GUI.hpp>
#include <slic3r/GUI/PresetBundle.hpp>
#include <slic3r/GUI/MsgDialog.hpp>

#include <PrintConfig.hpp>
#include <Print.hpp>
#include <Model.hpp>
#include <Utils.hpp>

#include <wx/app.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/gauge.h>
#include <wx/statusbr.h>

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

    wxMessageBox(_(description), _(brief), icon);
}

AppControllerBoilerplate::ProgresIndicatorPtr
AppControllerBoilerplate::createProgressIndicator(unsigned statenum,
                                       const std::string& title,
                                       const std::string& firstmsg) const
{
    class GuiProgressIndicator: public ProgressIndicator {
        wxProgressDialog gauge_;
        using Base = ProgressIndicator;
        std::string message_;
    public:

        inline GuiProgressIndicator(int range, const std::string& title,
                                    const std::string& firstmsg):
            gauge_(title, firstmsg, range, wxTheApp->GetTopWindow())
        {
            Base::max(static_cast<float>(range));
            Base::states(static_cast<unsigned>(range));
        }

        virtual void state(float val) override {
            if( val <= max() && val >= 1.0) {
                Base::state(val);
                gauge_.Update(static_cast<int>(val), message_);
            }
        }

        virtual void state(unsigned st) override {
            if( st <= max() ) {
                Base::state(st);
                gauge_.Update(static_cast<int>(st), message_);
                if(!gauge_.IsShown()) gauge_.ShowModal();
            }
        }

        virtual void message(const std::string & msg) override {
            message_ = msg;
        }

        virtual void title(const std::string & title) override {
            gauge_.SetTitle(title);
        }
    };

    auto pri =
            std::make_shared<GuiProgressIndicator>(statenum, title, firstmsg);

    return pri;
}

void AppController::sliceObject(PrintObject *pobj)
{
    assert(pobj != nullptr);
    if(pobj->state.is_done(posSlice)) return;

    pobj->state.set_started(posSlice);

    pobj->_slice();

    auto msg = pobj->_fix_slicing_errors();
    if(!msg.empty()) report_issue(IssueType::WARN, msg);

    // simplify slices if required
    if (print_->config.resolution)
        pobj->_simplify_slices(scale_(print_->config.resolution));


    if(pobj->layers.empty())
        report_issue(IssueType::ERR,
                     "No layers were detected. You might want to repair your "
                       "STL file(s) or check their size or thickness and retry"
                     );

    pobj->state.set_done(posSlice);
}

void AppController::slice()
{
    Slic3r::trace(3, "Starting the slicing process.");

    for(auto obj : print_->objects) {
        sliceObject(obj);
//        print_->//status_cb->(20, "Generating perimeters");
        obj->_make_perimeters();
    }

//   $self->status_cb->(70, "Infilling layers");

//   $_->infill for @{$self->objects};

//   $_->generate_support_material for @{$self->objects};
//   $self->make_skirt;
//   $self->make_brim;  # must come after make_skirt
//   $self->make_wipe_tower;

//   # time to make some statistics
//   if (0) {
//       eval "use Devel::Size";
//       print  "MEMORY USAGE:\n";
//       printf "  meshes        = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->meshes), @{$self->objects})/1024/1024;
//       printf "  layer slices  = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->layers}, @{$self->objects})/1024/1024;
//       printf "  region slices = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
//       printf "  perimeters    = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->perimeters), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
//       printf "  fills         = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->fills), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
//       printf "  print object  = %.1fMb\n", Devel::Size::total_size($self)/1024/1024;
//   }
//   if (0) {
//       eval "use Slic3r::Test::SectionCut";
//       Slic3r::Test::SectionCut->new(print => $self)->export_svg("section_cut.svg");
//   }
//   Slic3r::trace(3, "Slicing process finished.")
}

void AppController::slice_to_png()
{
    assert(model_ != nullptr);

    auto pri = globalProgressIndicator(); //createProgressIndicator(100, "Gauge", "operation");

    pri->title("Operation");
    pri->message("...");

    for(unsigned i = 1; i <= 100; i++ ) {
        pri->state(i);
        wxMilliSleep(100);
    }

//    auto zipfilepath = query_destination_path(  "Path to zip file...",
//                                                "*.zip");

//    auto presetbundle = GUI::get_preset_bundle();

//    assert(presetbundle);

//    auto conf = presetbundle->full_config();

//    conf.validate();

//    slice();
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

                if(st == gauge_->GetRange()) {
                    gauge_->SetValue(0);
                    showProgress(false);
                } else {
                    stbar_->SetStatusText(message_);
                    gauge_->SetValue(static_cast<int>(st));
                }
            }
        }

        virtual void message(const std::string & msg) override {
            message_ = msg;
        }

        virtual void title(const std::string & /*title*/) override {}

    };

    wxGauge* gauge = dynamic_cast<wxGauge*>(wxWindow::FindWindowById(gid));
    wxStatusBar* sb = dynamic_cast<wxStatusBar*>(wxWindow::FindWindowById(sid));

    if(gauge && sb)
        globalProgressIndicator(std::make_shared<Wrapper>(gauge, sb));
}

}
