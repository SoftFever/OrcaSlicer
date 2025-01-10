#ifndef HAS_WIN10SDK

#include "FixModel.hpp"

#include <condition_variable>
#include <exception>
#include <thread>
#include <cstdio>

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "../GUI/GUI.hpp"
#include "../GUI/I18N.hpp"
#include "../GUI/MsgDialog.hpp"

namespace Slic3r {

bool is_repair_available()
{
    static int test_executed = 0;

    if (test_executed == 0) {
        bool found = false;
        int r;
        char *line = nullptr;
        size_t linelen = 0;

        const std::string marker{"MeshLabServer version:"};

        FILE *f = popen("meshlabserver", "r");
        if (!f)
            return false;

        while((r = getline(&line, &linelen, f)) > 0) {
            //fprintf(stderr, "%i: %s", r, line);

            if (strncmp(line, marker.c_str(), marker.size()) == 0) {
                found = true;
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << line;
                // do not break, read output till the end
            }
        }

        free(line);
        pclose(f);

        test_executed = found ? 1 : -1;
    }

    return test_executed > 0;
}

typedef std::function<void ()> ThrowOnCancelFn;

class RepairCanceledException : public std::exception {
public:
    const char* what() const throw() { return "Model repair has been canceled"; }
};

void fix_model_by_meshlab(const std::string &path_src, const std::string &path_dst, ThrowOnCancelFn throw_on_cancel)
{
    // TODO: actual meshlabserver call
    std::string cmd = "/usr/bin/cp -f " + path_src + " " + path_dst;
    fprintf(stderr, "%s\n", cmd.c_str());
    if (system(cmd.c_str())) {
        throw_on_cancel();
    }
}

// returt FALSE, if fixing was canceled
// fix_result is empty, if fixing finished successfully
// fix_result containes a message if fixing failed
bool fix_model(ModelObject &model_object, int volume_idx, GUI::ProgressDialog& progress_dialog, const wxString& msg_header, std::string& fix_result)
{
    std::mutex mtx;
    std::condition_variable condition;
    struct Progress {
        std::string 				message;
        int 						percent  = 0;
        bool						updated = false;
    } progress;
    std::atomic<bool>				canceled = false;
    std::atomic<bool>				finished = false;
    std::vector<ModelVolume*> volumes;
    if (volume_idx == -1)
        volumes = model_object.volumes;
    else
        volumes.emplace_back(model_object.volumes[volume_idx]);

    // Executing the calculation in a background thread, so that the COM context could be created with its own threading model.
    // (It seems like wxWidgets initialize the COM contex as single threaded and we need a multi-threaded context).
    bool   success = false;
    size_t ivolume = 0;
    auto on_progress = [&mtx, &condition, &ivolume, &volumes, &progress](const char *msg, unsigned prcnt) {
        std::unique_lock<std::mutex> lock(mtx);
        progress.message = msg;
        progress.percent = (int)floor((float(prcnt) + float(ivolume) * 100.f) / float(volumes.size()));
        progress.updated = true;
        condition.notify_all();
    };
    auto worker_thread = boost::thread([&model_object, &volumes, &ivolume, on_progress, &success, &canceled, &finished]() {
        try {
            std::vector<TriangleMesh> meshes_repaired;
            meshes_repaired.reserve(volumes.size());
            int progress_step = 80 / volumes.size() / 2;
            int progress = 0;
            on_progress(L("Repair started"), 0);
            for (; ivolume < volumes.size(); ++ ivolume) {
                boost::filesystem::path path_src = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
                path_src += ".stl";

                TriangleMesh mesh = volumes[ivolume]->mesh();

                if (!Slic3r::store_stl(path_src.c_str(), &mesh, true)) {
                    boost::filesystem::remove(path_src);
                    throw Slic3r::RuntimeError(L("Exporting STL mesh failed"));
                }

                boost::filesystem::path path_dst = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
                path_dst += ".stl";

                on_progress(L("Exported object #d for repair"), (progress += progress_step));

                fix_model_by_meshlab(path_src.string(), path_dst.string(),
                                     [&canceled]() { if (canceled) throw RepairCanceledException(); });

                on_progress(L("Loading repaired object #d"), (progress += progress_step));

                Model model;
                bool loaded = Slic3r::load_stl(path_dst.string().c_str(), &model);

                boost::filesystem::remove(path_src);
                boost::filesystem::remove(path_dst);

                if (! loaded)
                    throw Slic3r::RuntimeError(L("Import STL mesh failed"));

                meshes_repaired.emplace_back(std::move(model.objects.front()->volumes.front()->mesh()));
            }
            for (size_t i = 0; i < volumes.size(); ++ i) {
                volumes[i]->set_mesh(std::move(meshes_repaired[i]));
                volumes[i]->calculate_convex_hull();
                volumes[i]->invalidate_convex_hull_2d();
                volumes[i]->set_new_unique_id();
            }
            model_object.invalidate_bounding_box();
            -- ivolume;
            on_progress(L("Repair finished"), 100);
            success  = true;
            finished = true;
        } catch (RepairCanceledException & /* ex */) {
            canceled = true;
            finished = true;
            on_progress(L("Repair canceled"), 100);
        } catch (std::exception &ex) {
            success = false;
            finished = true;
            on_progress(ex.what(), 100);
        }
    });
    while (! finished) {
        std::unique_lock<std::mutex> lock(mtx);
        condition.wait_for(lock, std::chrono::milliseconds(250), [&progress]{ return progress.updated; });
        // decrease progress.percent value to avoid closing of the progress dialog
        if (!progress_dialog.Update(progress.percent-1, msg_header + _(progress.message)))
            canceled = true;
        else
            progress_dialog.Fit();
        progress.updated = false;
    }

    if (canceled) {
        // Nothing to show.
    } else if (success) {
        fix_result = "";
    } else {
        fix_result = progress.message;
    }
    worker_thread.join();
    return !canceled;
}

} // namespace Slic3r

#endif // ifndef HAS_WIN10SDK
