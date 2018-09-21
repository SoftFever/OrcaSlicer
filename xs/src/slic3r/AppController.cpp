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


namespace Slic3r {

class AppControllerGui::PriData {
public:
    std::mutex m;
    std::thread::id ui_thread;

    inline explicit PriData(std::thread::id uit): ui_thread(uit) {}
};

AppControllerGui::AppControllerGui()
    :m_pri_data(new PriData(std::this_thread::get_id())) {}

AppControllerGui::~AppControllerGui() {
    m_pri_data.reset();
}

bool AppControllerGui::is_main_thread() const
{
    return m_pri_data->ui_thread == std::this_thread::get_id();
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

ProgresIndicatorPtr AppControllerGui::global_progress_indicator() {
    ProgresIndicatorPtr ret;

    m_pri_data->m.lock();
    ret = m_global_progressind;
    m_pri_data->m.unlock();

    return ret;
}

void AppControllerGui::global_progress_indicator(ProgresIndicatorPtr gpri)
{
    m_pri_data->m.lock();
    m_global_progressind = gpri;
    m_pri_data->m.unlock();
}

PrintController::PngExportData
PrintController::query_png_export_data(const DynamicPrintConfig& conf)
{
    PngExportData ret;

    auto c = GUI::get_appctl();
    auto zippath = c->query_destination_path("Output zip file", "*.zip",
                                             "export-png",
                                             "out");

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

void PrintController::slice(ProgresIndicatorPtr pri)
{
    m_print->set_status_callback([pri](int st, const std::string& msg){
        pri->update(unsigned(st), msg);
    });

    m_print->process();
}

void PrintController::slice()
{
    auto ctl = GUI::get_appctl();
    auto pri = ctl->global_progress_indicator();
    if(!pri) pri = ctl->create_progress_indicator(100, L("Slicing"));
    slice(pri);
}

template<> class LayerWriter<Zipper> {
    Zipper m_zip;
public:

    inline LayerWriter(const std::string& zipfile_path): m_zip(zipfile_path) {}

    inline void next_entry(const std::string& fname) { m_zip.next_entry(fname); }

    inline std::string get_name() const { return m_zip.get_name(); }

    template<class T> inline LayerWriter& operator<<(const T& arg) {
        m_zip.stream() << arg; return *this;
    }

    inline void close() { m_zip.close(); }
};

void PrintController::slice_to_png()
{
    using Pointf3 = Vec3d;

    auto ctl = GUI::get_appctl();
    auto presetbundle = GUI::get_preset_bundle();

    assert(presetbundle);

    // FIXME: this crashes in command line mode
    auto pt = presetbundle->printers.get_selected_preset().printer_technology();
    if(pt != ptSLA) {
        ctl->report_issue(IssueType::ERR, L("Printer technology is not SLA!"),
                     L("Error"));
        return;
    }

    auto conf = presetbundle->full_config();
    conf.validate();

    auto exd = query_png_export_data(conf);
    if(exd.zippath.empty()) return;

    Print *print = m_print;

    try {
        print->apply_config(conf);
        print->validate();
    } catch(std::exception& e) {
        ctl->report_issue(IssueType::ERR, e.what(), "Error");
        return;
    }

    // TODO: copy the model and work with the copy only
    bool correction = false;
    if(exd.corr_x != 1.0 || exd.corr_y != 1.0 || exd.corr_z != 1.0) {
        correction = true;
//        print->invalidate_all_steps();

//        for(auto po : print->objects) {
//            po->model_object()->scale(
//                        Pointf3(exd.corr_x, exd.corr_y, exd.corr_z)
//                        );
//            po->model_object()->invalidate_bounding_box();
//            po->reload_model_instances();
//            po->invalidate_all_steps();
//        }
    }

    // Turn back the correction scaling on the model.
    auto scale_back = [this, print, correction, exd]() {
        if(correction) { // scale the model back
//            print->invalidate_all_steps();
//            for(auto po : print->objects) {
//                po->model_object()->scale(
//                    Pointf3(1.0/exd.corr_x, 1.0/exd.corr_y, 1.0/exd.corr_z)
//                );
//                po->model_object()->invalidate_bounding_box();
//                po->reload_model_instances();
//                po->invalidate_all_steps();
//            }
        }
    };

    auto print_bb = print->bounding_box();
    Vec2d punsc = unscale(print_bb.size());

    // If the print does not fit into the print area we should cry about it.
    if(px(punsc) > exd.width_mm || py(punsc) > exd.height_mm) {
        std::stringstream ss;

        ss << L("Print will not fit and will be truncated!") << "\n"
           << L("Width needed: ") << px(punsc) << " mm\n"
           << L("Height needed: ") << py(punsc) << " mm\n";

       if(!ctl->report_issue(IssueType::WARN_Q, ss.str(), L("Warning")))  {
            scale_back();
            return;
       }
    }

    auto pri = ctl->create_progress_indicator(
                200, L("Slicing to zipped png files..."));

    pri->on_cancel([&print](){ print->cancel(); });

    try {
        pri->update(0, L("Slicing..."));
        slice(pri);
    } catch (std::exception& e) {
        ctl->report_issue(IssueType::ERR, e.what(), L("Exception occurred"));
        scale_back();
        if(print->canceled()) print->restart();
        return;
    }

    auto initstate = unsigned(pri->state());
    print->set_status_callback([pri, initstate](int st, const std::string& msg)
    {
        pri->update(initstate + unsigned(st), msg);
    });

    try {
        print_to<FilePrinterFormat::PNG, Zipper>( *print, exd.zippath,
                    exd.width_mm, exd.height_mm,
                    exd.width_px, exd.height_px,
                    exd.exp_time_s, exd.exp_time_first_s);

    } catch (std::exception& e) {
        ctl->report_issue(IssueType::ERR, e.what(), L("Exception occurred"));
    }

    scale_back();
    if(print->canceled()) print->restart();
    print->set_status_default();
}

const PrintConfig &PrintController::config() const
{
    return m_print->config();
}

void ProgressIndicator::message_fmt(
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

void AppController::arrange_model()
{
    using Coord = libnest2d::TCoord<libnest2d::PointImpl>;

    auto ctl = GUI::get_appctl();

    if(m_arranging.load()) return;

    // to prevent UI reentrancies
    m_arranging.store(true);

    unsigned count = 0;
    for(auto obj : m_model->objects) count += obj->instances.size();

    auto pind = ctl->global_progress_indicator();

    float pmax = 1.0;

    if(pind) {
        pmax = pind->max();

        // Set the range of the progress to the object count
        pind->max(count);

        pind->on_cancel([this](){
            m_arranging.store(false);
        });
    }

    auto dist = print_ctl()->config().min_object_distance();

    // Create the arranger config
    auto min_obj_distance = static_cast<Coord>(dist/SCALING_FACTOR);

    auto& bedpoints = print_ctl()->config().bed_shape.values;
    Polyline bed; bed.points.reserve(bedpoints.size());
    for(auto& v : bedpoints)
        bed.append(Point::new_scale(v(0), v(1)));

    if(pind) pind->update(0, L("Arranging objects..."));

    try {
        arr::BedShapeHint hint;
        // TODO: from Sasha from GUI
        hint.type = arr::BedShapeType::WHO_KNOWS;

        arr::arrange(*m_model,
                      min_obj_distance,
                      bed,
                      hint,
                      false, // create many piles not just one pile
                      [this, pind, &ctl, count](unsigned rem) {
            if(pind)
                pind->update(count - rem, L("Arranging objects..."));

            ctl->process_events();
        }, [this] () { return !m_arranging.load(); });
    } catch(std::exception& e) {
        std::cerr << e.what() << std::endl;
        ctl->report_issue(IssueType::ERR,
                        L("Could not arrange model objects! "
                        "Some geometries may be invalid."),
                        L("Exception occurred"));
    }

    // Restore previous max value
    if(pind) {
        pind->max(pmax);
        pind->update(0, m_arranging.load() ? L("Arranging done.") :
                                            L("Arranging canceled."));

        pind->on_cancel(/*remove cancel function*/);
    }

    m_arranging.store(false);
}

}
