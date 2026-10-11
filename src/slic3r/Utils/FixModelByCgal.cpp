#include "FixModelByCgal.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <wx/string.h>

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/MeshRepair.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"

namespace Slic3r {

namespace {

class RepairCanceledException : public std::exception
{
public:
    const char *what() const noexcept override { return "Model repair has been canceled"; }
};

} // namespace

bool fix_model_with_cgal_gui(ModelObject &model_object, int volume_idx, GUI::ProgressDialog &progress_dialog, const wxString &msg_header, std::string &fix_result, bool keep_painting)
{
    // Keeps the backup manager from reading a mesh while the worker replaces it.
    SaveObjectGaurd backup_gaurd(model_object);

    std::vector<ModelVolume *> volumes;
    if (volume_idx < 0)
        volumes = model_object.volumes;
    else if (std::size_t(volume_idx) < model_object.volumes.size())
        volumes.push_back(model_object.volumes[volume_idx]);

    std::mutex              mtx;
    std::condition_variable condition;
    struct Progress
    {
        std::string message;
        int         percent = 0;
        bool        updated = false;
    } progress;
    std::atomic<bool> canceled = false;
    std::atomic<bool> finished = false;
    std::string       errors;

    auto on_progress = [&mtx, &condition, &progress](const std::string &msg, int percent) {
        std::lock_guard<std::mutex> lock(mtx);
        progress.message = msg;
        progress.percent = percent;
        progress.updated = true;
        condition.notify_all();
    };

    auto worker_thread = std::thread([&]() {
        set_current_thread_name("cgal_fix_model");
        const auto throw_on_cancel = [&canceled] {
            if (canceled)
                throw RepairCanceledException();
        };
        try {
            for (std::size_t i = 0; i < volumes.size(); ++i) {
                ModelVolume *volume = volumes[i];
                on_progress(volumes.size() > 1 ? volume->name : std::string(), int(100 * i / volumes.size()));

                TriangleMesh    mesh = volume->mesh();
                MeshRepairStats stats;
                std::string     error;
                if (!repair_mesh(mesh, &stats, &error, throw_on_cancel)) {
                    errors += (errors.empty() ? "" : "\n") + (volumes.size() > 1 ? volume->name + ": " : std::string()) + error;
                    continue;
                }
                if (!stats.modified)
                    continue;

                const std::optional<TriangleSelector::SavedPainting> saved_painting = keep_painting ? volume->save_painting() : std::nullopt;
                volume->set_mesh(std::move(mesh));
                volume->calculate_convex_hull();
                volume->invalidate_convex_hull_2d();
                volume->set_new_unique_id();
                volume->restore_painting(saved_painting);
            }
            model_object.invalidate_bounding_box();
        } catch (const RepairCanceledException &) {
            canceled = true;
        } catch (const std::exception &ex) {
            errors += (errors.empty() ? "" : "\n") + std::string(ex.what());
        }
        finished = true;
        on_progress(_u8L("Repair finished"), 100);
    });

    while (!finished) {
        std::unique_lock<std::mutex> lock(mtx);
        condition.wait_for(lock, std::chrono::milliseconds(250), [&progress] { return progress.updated; });
        // One percent short of the maximum, so that the dialog does not close itself.
        if (!progress_dialog.Update(std::max(0, progress.percent - 1), msg_header + wxString::FromUTF8(progress.message)))
            canceled = true;
        else
            progress_dialog.Fit();
        progress.updated = false;
    }

    if (worker_thread.joinable())
        worker_thread.join();

    fix_result = canceled ? std::string() : errors;
    return !canceled;
}

} // namespace Slic3r
