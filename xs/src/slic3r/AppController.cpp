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

#include <Geometry.hpp>
#include <PrintConfig.hpp>
#include <Print.hpp>
#include <Model.hpp>
#include <Utils.hpp>

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

const PrintConfig &PrintController::config() const
{
    return print_->config;
}

void AppController::arrange_model()
{
    auto ftr = std::async(
               supports_asynch()? std::launch::async : std::launch::deferred,
               [this]()
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

        }

        auto dist = print_ctl()->config().min_object_distance();

        // Create the arranger config
        auto min_obj_distance = static_cast<Coord>(dist/SCALING_FACTOR);

        auto& bedpoints = print_ctl()->config().bed_shape.values;
        Polyline bed; bed.points.reserve(bedpoints.size());
        for(auto& v : bedpoints)
            bed.append(Point::new_scale(v.x, v.y));

        if(pind) pind->update(0, L("Arranging objects..."));

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
                    pind->update(count - rem, L("Arranging objects..."));
            });
        } catch(std::exception& e) {
            std::cerr << e.what() << std::endl;
            report_issue(IssueType::ERR,
                         L("Could not arrange model objects! "
                         "Some geometries may be invalid."),
                         L("Exception occurred"));
        }

        // Restore previous max value
        if(pind) {
            pind->max(pmax);
            pind->update(0, L("Arranging done."));
        }
    });

    while( ftr.wait_for(std::chrono::milliseconds(10))
           != std::future_status::ready) {
        process_events();
    }
}

}
