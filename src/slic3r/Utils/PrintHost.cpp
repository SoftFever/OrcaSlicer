#include "PrintHost.hpp"

#include <vector>
#include <thread>
#include <exception>
#include <boost/optional.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

#include <wx/string.h>
#include <wx/app.h>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Channel.hpp"
#include "OctoPrint.hpp"
#include "Duet.hpp"
#include "../GUI/PrintHostDialogs.hpp"

namespace fs = boost::filesystem;
using boost::optional;
using Slic3r::GUI::PrintHostQueueDialog;

namespace Slic3r {


PrintHost::~PrintHost() {}

PrintHost* PrintHost::get_print_host(DynamicPrintConfig *config)
{
    const auto opt = config->option<ConfigOptionEnum<PrintHostType>>("host_type");
    if (opt == nullptr) { return nullptr; }

    switch (opt->value) {
        case htOctoPrint: return new OctoPrint(config);
        case htDuet:      return new Duet(config);
        case htSL1:       return new SLAHost(config);
        default: return nullptr;
    }
}


struct PrintHostJobQueue::priv
{
    // XXX: comment on how bg thread works

    PrintHostJobQueue *q;

    Channel<PrintHostJob> channel_jobs;
    Channel<size_t> channel_cancels;
    size_t job_id = 0;
    int prev_progress = -1;

    std::thread bg_thread;
    bool bg_exit = false;

    PrintHostQueueDialog *queue_dialog;

    priv(PrintHostJobQueue *q) : q(q) {}

    void start_bg_thread();
    void bg_thread_main();
    void progress_fn(Http::Progress progress, bool &cancel);
    void perform_job(PrintHostJob the_job);
};

PrintHostJobQueue::PrintHostJobQueue(PrintHostQueueDialog *queue_dialog)
    : p(new priv(this))
{
    p->queue_dialog = queue_dialog;
}

PrintHostJobQueue::~PrintHostJobQueue()
{
    if (p && p->bg_thread.joinable()) {
        p->bg_exit = true;
        p->channel_jobs.push(PrintHostJob()); // Push an empty job to wake up bg_thread in case it's sleeping
        p->bg_thread.detach();                // Let the background thread go, it should exit on its own
    }
}

void PrintHostJobQueue::priv::start_bg_thread()
{
    if (bg_thread.joinable()) { return; }

    std::shared_ptr<priv> p2 = q->p;
    bg_thread = std::thread([p2]() {
        p2->bg_thread_main();
    });
}

void PrintHostJobQueue::priv::bg_thread_main()
{
    // bg thread entry point

    try {
        // Pick up jobs from the job channel:
        while (! bg_exit) {
            auto job = channel_jobs.pop();   // Sleeps in a cond var if there are no jobs
            if (! job.cancelled) {
                perform_job(std::move(job));
            }
            job_id++;
        }
    } catch (const std::exception &e) {
        auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_ERROR, queue_dialog->GetId(), job_id, e.what());
        wxQueueEvent(queue_dialog, evt);
    } catch (...) {
        wxTheApp->OnUnhandledException();
    }
}

void PrintHostJobQueue::priv::progress_fn(Http::Progress progress, bool &cancel)
{
    if (bg_exit) {
        cancel = true;
        return;
    }

    if (channel_cancels.size_hint() > 0) {
        // Lock both queues
        auto cancels = channel_cancels.lock_rw();
        auto jobs = channel_jobs.lock_rw();

        for (size_t cancel_id : *cancels) {
            if (cancel_id == job_id) {
                cancel = true;
            } else if (cancel_id > job_id) {
                jobs->at(cancel_id - job_id).cancelled = true;
            }
        }

        cancels->clear();
    }

    int gui_progress = progress.ultotal > 0 ? 100*progress.ulnow / progress.ultotal : 0;
    if (gui_progress != prev_progress) {
        auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_PROGRESS, queue_dialog->GetId(), job_id, gui_progress);
        wxQueueEvent(queue_dialog, evt);
        prev_progress = gui_progress;
    }
}

void PrintHostJobQueue::priv::perform_job(PrintHostJob the_job)
{
    if (bg_exit || the_job.empty()) { return; }

    BOOST_LOG_TRIVIAL(debug) << boost::format("PrintHostJobQueue/bg_thread: Got job: `%1%` -> `%2%`")
        % the_job.upload_data.upload_path
        % the_job.printhost->get_host();

    const fs::path gcode_path = the_job.upload_data.source_path;

    bool success = the_job.printhost->upload(std::move(the_job.upload_data),
        [this](Http::Progress progress, bool &cancel) { this->progress_fn(std::move(progress), cancel); },
        [this](wxString error) {
            auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_ERROR, queue_dialog->GetId(), job_id, std::move(error));
            wxQueueEvent(queue_dialog, evt);
        }
    );

    if (success) {
        auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_PROGRESS, queue_dialog->GetId(), job_id, 100);
        wxQueueEvent(queue_dialog, evt);
    }

    boost::system::error_code ec;
    fs::remove(gcode_path, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << boost::format("PrintHostJobQueue: Error removing file `%1%`: %2%") % gcode_path % ec;
    }
}

void PrintHostJobQueue::enqueue(PrintHostJob job)
{
    p->start_bg_thread();
    p->queue_dialog->append_job(job);
    p->channel_jobs.push(std::move(job));
}


}
