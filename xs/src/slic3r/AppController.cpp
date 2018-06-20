#include "AppController.hpp"

#include <future>
#include <thread>
#include <sstream>
#include <cstdarg>

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

static const PrintObjectStep STEP_SLICE                 = posSlice;
static const PrintObjectStep STEP_PERIMETERS            = posPerimeters;
static const PrintObjectStep STEP_PREPARE_INFILL        = posPrepareInfill;
static const PrintObjectStep STEP_INFILL                = posInfill;
static const PrintObjectStep STEP_SUPPORTMATERIAL       = posSupportMaterial;
static const PrintStep STEP_SKIRT                       = psSkirt;
static const PrintStep STEP_BRIM                        = psBrim;
static const PrintStep STEP_WIPE_TOWER                  = psWipeTower;

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

AppControllerBoilerplate::ProgresIndicatorPtr
AppControllerBoilerplate::createProgressIndicator(unsigned statenum,
                                       const std::string& title,
                                       const std::string& firstmsg) const
{
    class GuiProgressIndicator: public ProgressIndicator {
        wxProgressDialog gauge_;
        using Base = ProgressIndicator;
        wxString message_;
    public:

        inline GuiProgressIndicator(int range, const std::string& title,
                                    const std::string& firstmsg):
            gauge_(_(title), _(firstmsg), range, wxTheApp->GetTopWindow())
        {
            gauge_.Show(false);
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
                if(!gauge_.IsShown()) gauge_.ShowModal();
                Base::state(st);
                gauge_.Update(static_cast<int>(st), message_);
            }
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
            gauge_.SetTitle(_(title));
        }
    };

    auto pri =
            std::make_shared<GuiProgressIndicator>(statenum, title, firstmsg);

    return pri;
}

void PrintController::make_skirt()
{
    assert(print_ != nullptr);

    // prerequisites
    for(auto obj : print_->objects) make_perimeters(obj);
    for(auto obj : print_->objects) infill(obj);
    for(auto obj : print_->objects) gen_support_material(obj);

    if(!print_->state.is_done(STEP_SKIRT)) {
        print_->state.set_started(STEP_SKIRT);
        print_->skirt.clear();
        if(print_->has_skirt()) print_->_make_skirt();

        print_->state.set_done(STEP_SKIRT);
    }
}

void PrintController::make_brim()
{
    assert(print_ != nullptr);

    // prerequisites
    for(auto obj : print_->objects) make_perimeters(obj);
    for(auto obj : print_->objects) infill(obj);
    for(auto obj : print_->objects) gen_support_material(obj);
    make_skirt();

    if(!print_->state.is_done(STEP_BRIM)) {
        print_->state.set_started(STEP_BRIM);

        // since this method must be idempotent, we clear brim paths *before*
        // checking whether we need to generate them
        print_->brim.clear();

        if(print_->config.brim_width > 0) print_->_make_brim();

        print_->state.set_done(STEP_BRIM);
    }
}

void PrintController::make_wipe_tower()
{
    assert(print_ != nullptr);

    // prerequisites
    for(auto obj : print_->objects) make_perimeters(obj);
    for(auto obj : print_->objects) infill(obj);
    for(auto obj : print_->objects) gen_support_material(obj);
    make_skirt();
    make_brim();

    if(!print_->state.is_done(STEP_WIPE_TOWER)) {
        print_->state.set_started(STEP_WIPE_TOWER);

        // since this method must be idempotent, we clear brim paths *before*
        // checking whether we need to generate them
        print_->brim.clear();

        if(print_->has_wipe_tower()) print_->_make_wipe_tower();

        print_->state.set_done(STEP_WIPE_TOWER);
    }
}

void PrintController::slice(PrintObject *pobj)
{
    assert(pobj != nullptr && print_ != nullptr);

    if(pobj->state.is_done(STEP_SLICE)) return;

    pobj->state.set_started(STEP_SLICE);

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

    pobj->state.set_done(STEP_SLICE);
}

void PrintController::make_perimeters(PrintObject *pobj)
{
    assert(pobj != nullptr);

    slice(pobj);

    auto&& prgind = progressIndicator();

    if (!pobj->state.is_done(STEP_PERIMETERS)) {
        pobj->_make_perimeters();
    }
}

void PrintController::infill(PrintObject *pobj)
{
    assert(pobj != nullptr);

    make_perimeters(pobj);

    if (!pobj->state.is_done(STEP_PREPARE_INFILL)) {
        pobj->state.set_started(STEP_PREPARE_INFILL);

        pobj->_prepare_infill();

        pobj->state.set_done(STEP_PREPARE_INFILL);
    }

    pobj->_infill();
}

void PrintController::gen_support_material(PrintObject *pobj)
{
    assert(pobj != nullptr);

    // prerequisites
    slice(pobj);

    if(!pobj->state.is_done(STEP_SUPPORTMATERIAL)) {
        pobj->state.set_started(STEP_SUPPORTMATERIAL);

        pobj->clear_support_layers();

        if((pobj->config.support_material || pobj->config.raft_layers > 0)
                && pobj->layers.size() > 1) {
            pobj->_generate_support_material();
        }

        pobj->state.set_done(STEP_SUPPORTMATERIAL);
    }
}

void PrintController::slice()
{
    Slic3r::trace(3, "Starting the slicing process.");

    progressIndicator()->update(20u, "Generating perimeters");
    for(auto obj : print_->objects) make_perimeters(obj);

    progressIndicator()->update(60u, "Infilling layers");
    for(auto obj : print_->objects) infill(obj);

    progressIndicator()->update(70u, "Generating support material");
    for(auto obj : print_->objects) gen_support_material(obj);

    progressIndicator()->messageFmt("Weight: %.1fg, Cost: %.1f",
                                    print_->total_weight,
                                    print_->total_cost);

    progressIndicator()->state(85u);


    progressIndicator()->update(88u, "Generating skirt");
    make_skirt();


    progressIndicator()->update(90u, "Generating brim");
    make_brim();

    progressIndicator()->update(95u, "Generating wipe tower");
    make_wipe_tower();

    progressIndicator()->update(100u, "Done");

    // time to make some statistics..

    Slic3r::trace(3, "Slicing process finished.");
}

void PrintController::slice_to_png()
{
    assert(model_ != nullptr);

//    auto pri = globalProgressIndicator();

//    pri->title("Operation");
//    pri->message("...");

//    for(unsigned i = 1; i <= 100; i++ ) {
//        pri->state(i);
//        wxMilliSleep(100);
//    }

//    auto zipfilepath = query_destination_path(  "Path to zip file...",
//                                                "*.zip");

    auto presetbundle = GUI::get_preset_bundle();

    assert(presetbundle);

    auto conf = presetbundle->full_config();

    conf.validate();

    auto bak = progressIndicator();
    progressIndicator(100, "Slicing to zipped png files...");
    std::async(std::launch::async, [this, &bak](){
        slice();
        progressIndicator(bak);
    });

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

void AppControllerBoilerplate::ProgressIndicator::messageFmt(
        const std::string &fmtstr, ...) {
    std::stringstream ss;
    va_list args;
    va_start(args, fmtstr);

    auto fmt = fmtstr.begin();

    while (*fmt != '\0') {
        if (*fmt == 'd') {
            int i = va_arg(args, int);
            ss << i << '\n';
        } else if (*fmt == 'c') {
            // note automatic conversion to integral type
            int c = va_arg(args, int);
            ss << static_cast<char>(c) << '\n';
        } else if (*fmt == 'f') {
            double d = va_arg(args, double);
            ss << d << '\n';
        }
        ++fmt;
    }

    va_end(args);
    message(ss.str());
}

}
