#include "AppController.hpp"

#include <future>
#include <chrono>
#include <sstream>
#include <cstdarg>
#include <thread>
#include <unordered_map>

#include <slic3r/GUI/GUI.hpp>
#include <ModelArrange.hpp>
#include <slic3r/GUI/PresetBundle.hpp>

#include <PrintConfig.hpp>
#include <Print.hpp>
#include <PrintExport.hpp>
#include <Geometry.hpp>
#include <Model.hpp>
#include <Utils.hpp>
#include <SLABasePool.hpp>

namespace Slic3r {

class AppControllerBoilerplate::PriData {
public:
    std::mutex m;
    std::thread::id ui_thread;

    inline explicit PriData(std::thread::id uit): ui_thread(uit) {}
};

AppControllerBoilerplate::AppControllerBoilerplate()
    :pri_data_(new PriData(std::this_thread::get_id())) {}

AppControllerBoilerplate::~AppControllerBoilerplate() {
    pri_data_.reset();
}

bool AppControllerBoilerplate::is_main_thread() const
{
    return pri_data_->ui_thread == std::this_thread::get_id();
}

namespace GUI {
PresetBundle* get_preset_bundle();
}

static const PrintObjectStep STEP_SLICE                 = posSlice;
static const PrintObjectStep STEP_PERIMETERS            = posPerimeters;
static const PrintObjectStep STEP_PREPARE_INFILL        = posPrepareInfill;
static const PrintObjectStep STEP_INFILL                = posInfill;
static const PrintObjectStep STEP_SUPPORTMATERIAL       = posSupportMaterial;
static const PrintStep STEP_SKIRT                       = psSkirt;
static const PrintStep STEP_BRIM                        = psBrim;
static const PrintStep STEP_WIPE_TOWER                  = psWipeTower;

AppControllerBoilerplate::ProgresIndicatorPtr
AppControllerBoilerplate::global_progress_indicator() {
    ProgresIndicatorPtr ret;

    pri_data_->m.lock();
    ret = global_progressind_;
    pri_data_->m.unlock();

    return ret;
}

void AppControllerBoilerplate::global_progress_indicator(
        AppControllerBoilerplate::ProgresIndicatorPtr gpri)
{
    pri_data_->m.lock();
    global_progressind_ = gpri;
    pri_data_->m.unlock();
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
                     _(L("No layers were detected. You might want to repair your "
                     "STL file(s) or check their size or thickness and retry"))
                     );

    pobj->state.set_done(STEP_SLICE);
}

void PrintController::make_perimeters(PrintObject *pobj)
{
    assert(pobj != nullptr);

    slice(pobj);

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

PrintController::PngExportData
PrintController::query_png_export_data(const DynamicPrintConfig& conf)
{
    PngExportData ret;

    auto zippath = query_destination_path("Output zip file", "*.zip", "out");

    ret.zippath = zippath;

    ret.width_mm = conf.opt_float("display_width");
    ret.height_mm = conf.opt_float("display_height");

    ret.width_px = conf.opt_int("display_pixels_x");
    ret.height_px = conf.opt_int("display_pixels_y");

    auto opt_corr = conf.opt<ConfigOptionFloats>("printer_correction");

    if(opt_corr) {
        ret.corr_x = opt_corr->values[0];
        ret.corr_y = opt_corr->values[1];
        ret.corr_z = opt_corr->values[2];
    }

    ret.exp_time_first_s = conf.opt_float("initial_exposure_time");
    ret.exp_time_s = conf.opt_float("exposure_time");

    return ret;
}

void PrintController::slice(AppControllerBoilerplate::ProgresIndicatorPtr pri)
{
    auto st = pri->state();

    Slic3r::trace(3, "Starting the slicing process.");

    pri->update(st+20, _(L("Generating perimeters")));
    for(auto obj : print_->objects) make_perimeters(obj);

    pri->update(st+60, _(L("Infilling layers")));
    for(auto obj : print_->objects) infill(obj);

    pri->update(st+70, _(L("Generating support material")));
    for(auto obj : print_->objects) gen_support_material(obj);

    pri->message_fmt(_(L("Weight: %.1fg, Cost: %.1f")),
                     print_->total_weight, print_->total_cost);
    pri->state(st+85);


    pri->update(st+88, _(L("Generating skirt")));
    make_skirt();


    pri->update(st+90, _(L("Generating brim")));
    make_brim();

    pri->update(st+95, _(L("Generating wipe tower")));
    make_wipe_tower();

    pri->update(st+100, _(L("Done")));

    // time to make some statistics..

    Slic3r::trace(3, _(L("Slicing process finished.")));
}

void PrintController::slice()
{
    auto pri = global_progress_indicator();
    if(!pri) pri = create_progress_indicator(100, L("Slicing"));
    slice(pri);
}

void PrintController::slice_to_png()
{
    using Pointf3 = Vec3d;

    auto presetbundle = GUI::get_preset_bundle();

    assert(presetbundle);

    auto pt = presetbundle->printers.get_selected_preset().printer_technology();
    if(pt != ptSLA) {
        report_issue(IssueType::ERR, _("Printer technology is not SLA!"),
                     _("Error"));
        return;
    }

    auto conf = presetbundle->full_config();
    conf.validate();

    auto exd = query_png_export_data(conf);
    if(exd.zippath.empty()) return;

    try {
        print_->apply_config(conf);
        print_->validate();
    } catch(std::exception& e) {
        report_issue(IssueType::ERR, e.what(), "Error");
        return;
    }

    // TODO: copy the model and work with the copy only
    bool correction = false;
    if(exd.corr_x != 1.0 || exd.corr_y != 1.0 || exd.corr_z != 1.0) {
        correction = true;
        print_->invalidate_all_steps();

        for(auto po : print_->objects) {
            po->model_object()->scale(
                        Pointf3(exd.corr_x, exd.corr_y, exd.corr_z)
                        );
            po->model_object()->invalidate_bounding_box();
            po->reload_model_instances();
            po->invalidate_all_steps();
        }
    }

    // Turn back the correction scaling on the model.
    auto scale_back = [this, correction, exd]() {
        if(correction) { // scale the model back
            print_->invalidate_all_steps();
            for(auto po : print_->objects) {
                po->model_object()->scale(
                    Pointf3(1.0/exd.corr_x, 1.0/exd.corr_y, 1.0/exd.corr_z)
                );
                po->model_object()->invalidate_bounding_box();
                po->reload_model_instances();
                po->invalidate_all_steps();
            }
        }
    };

    auto print_bb = print_->bounding_box();
    Vec2d punsc = unscale(print_bb.size());

    // If the print does not fit into the print area we should cry about it.
    if(px(punsc) > exd.width_mm || py(punsc) > exd.height_mm) {
        std::stringstream ss;

        ss << _(L("Print will not fit and will be truncated!")) << "\n"
           << _(L("Width needed: ")) << px(punsc) << " mm\n"
           << _(L("Height needed: ")) << py(punsc) << " mm\n";

       if(!report_issue(IssueType::WARN_Q, ss.str(), _(L("Warning"))))  {
           scale_back();
           return;
       }
    }

//    std::async(supports_asynch()? std::launch::async : std::launch::deferred,
//                   [this, exd, scale_back]()
//    {

        auto pri = create_progress_indicator(
                    200, _(L("Slicing to zipped png files...")));

        try {
            pri->update(0, _(L("Slicing...")));
            slice(pri);
        } catch (std::exception& e) {
            pri->cancel();
            report_issue(IssueType::ERR, e.what(), _(L("Exception occured")));
            scale_back();
            return;
        }

        auto pbak = print_->progressindicator;
        print_->progressindicator = pri;

        try {
            print_to<FilePrinterFormat::PNG>( *print_, exd.zippath,
                        exd.width_mm, exd.height_mm,
                        exd.width_px, exd.height_px,
                        exd.exp_time_s, exd.exp_time_first_s);

        } catch (std::exception& e) {
            pri->cancel();
            report_issue(IssueType::ERR, e.what(), _(L("Exception occured")));
        }

        print_->progressindicator = pbak;
        scale_back();

//    });
}

void ProgressIndicator::message_fmt(
        const string &fmtstr, ...) {
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

const PrintConfig &PrintController::config() const
{
    return print_->config;
}


void AppController::arrange_model()
{
    using Coord = libnest2d::TCoord<libnest2d::PointImpl>;

    unsigned count = 0;
    for(auto obj : model_->objects) count += obj->instances.size();

    auto pind = global_progress_indicator();

    float pmax = 1.0;

    if(pind) {
        pmax = pind->max();

        // Set the range of the progress to the object count
        pind->max(count);

        pind->on_cancel([](){
            std::cout << "Cannot be cancelled!" << std::endl;
        });
    }

    auto dist = print_ctl()->config().min_object_distance();

    // Create the arranger config
    auto min_obj_distance = static_cast<Coord>(dist/SCALING_FACTOR);

    auto& bedpoints = print_ctl()->config().bed_shape.values;
    Polyline bed; bed.points.reserve(bedpoints.size());
    for(auto& v : bedpoints)
        bed.append(Point::new_scale(v(0), v(1)));

    if(pind) pind->update(0, _(L("Arranging objects...")));

    try {
        arr::BedShapeHint hint;
        // TODO: from Sasha from GUI
        hint.type = arr::BedShapeType::WHO_KNOWS;

        arr::arrange(*model_,
                      min_obj_distance,
                      bed,
                      hint,
                      false, // create many piles not just one pile
                      [pind, count](unsigned rem) {
            if(pind)
                pind->update(count - rem, _(L("Arranging objects...")));
        });
    } catch(std::exception& e) {
        std::cerr << e.what() << std::endl;
        report_issue(IssueType::ERR,
                        _(L("Could not arrange model objects! "
                        "Some geometries may be invalid.")),
                        _(L("Exception occurred")));
    }

    // Restore previous max value
    if(pind) {
        pind->max(pmax);
        pind->update(0, _(L("Arranging done.")));
        pind->on_cancel(/*remove cancel function*/);
    }
}

}
