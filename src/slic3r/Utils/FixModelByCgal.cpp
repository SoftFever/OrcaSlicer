#include "FixModelByCgal.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"
#include <string>
#include <thread>
#include <wx/string.h>

#include "libslic3r/Model.hpp"
#include "libslic3r/ModelObjectRepair.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Thread.hpp"
#include "../GUI/I18N.hpp"

// Orca: This file runs the CGAL model repair (libslic3r/ModelObjectRepair) on a worker thread behind a progress dialog.

namespace Slic3r {

// Orca: Exception class for handling user-initiated cancellation of model repair operations.
class RepairCanceledException : public std::exception {
public:
    const char* what() const noexcept override { return "Model repair has been canceled"; }
};

// Orca: Main function to repair model objects using CGAL, with progress dialog and cancellation support.
// Returns false if fixing was canceled. fix_result contains error message if failed.
bool fix_model_with_cgal_gui(ModelObject &model_object, int volume_idx, GUI::ProgressDialog &progress_dialog, const wxString &msg_header, std::string &fix_result, bool keep_painting,
                             bool *self_intersections_remain)
{
    // Hold SaveObjectGaurd to prevent backup manager from racing concurrent mesh mutations (use-after-free).
    SaveObjectGaurd backup_gaurd(model_object);

    // Orca: The worker repairs a copy. The progress dialog keeps processing events, which may draw model_object, so it
    // changes only on this thread once the worker has finished, and not at all when the repair fails or is canceled.
    Model        scratch;
    ModelObject &copy = *scratch.add_object(model_object);

    // Orca: Synchronization primitives for progress updates between worker thread and GUI.
    std::mutex mtx;
    std::condition_variable condition;
    struct Progress {
        std::string message;
        int         percent  = 0;
        bool        updated  = false;
    } progress;

    std::atomic<bool> canceled = false;
    std::atomic<bool> finished = false;

    bool                    success = false;
    ModelObjectRepairReport report;

    // Orca: Lambda for updating progress from worker thread.
    auto on_progress = [&mtx, &condition, &progress](const std::string &msg, int percent) {
        std::unique_lock<std::mutex> lock(mtx);
        progress.message = msg;
        progress.percent = percent;
        progress.updated = true;
        condition.notify_all();
    };

    // Orca: Worker thread that performs the actual model repair operations.
    auto worker_thread = std::thread([&copy, volume_idx, keep_painting, on_progress, &report, &success, &canceled, &finished, &fix_result]() {
        try {
	        set_current_thread_name("cgal_fix_model");

            report = repair_model_object(copy, volume_idx, keep_painting, on_progress, [&canceled] {
                if (canceled)
                    throw RepairCanceledException();
            });
            success = true;
            finished = true;
        } catch (RepairCanceledException &) {
            canceled = true;
            finished = true;
            on_progress(_u8L("Repair canceled"), 100);
        } catch (std::exception &ex) {
            success = false;
            finished = true;
            fix_result = ex.what();
            on_progress(ex.what(), 100);
        }
    });

    // Orca: Main GUI loop to update progress dialog and handle cancellation.
    while (!finished) {
        std::unique_lock<std::mutex> lock(mtx);
        condition.wait_for(lock, std::chrono::milliseconds(250), [&progress]{ return progress.updated; });

        // Decrease progress percent slightly to avoid auto-closing.
        if (!progress_dialog.Update(progress.percent - 1, msg_header + _(progress.message)))
            canceled = true;
        else
            progress_dialog.Fit();

        progress.updated = false;
    }

    if (worker_thread.joinable())
        worker_thread.join();

    if (canceled)
        return false;
    if (success) {
        apply_repaired_volumes(model_object, copy);
        fix_result.clear();
        if (self_intersections_remain)
            *self_intersections_remain = report.self_intersections_remain;
    }
    return true;
}

} // namespace Slic3r
