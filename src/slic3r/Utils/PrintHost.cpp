#include "PrintHost.hpp"

#include <vector>
#include <thread>
#include <exception>
#include <boost/optional.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

#include <wx/string.h>
#include <wx/app.h>
#include <wx/arrstr.h>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Channel.hpp"
#include "OctoPrint.hpp"
#include "Duet.hpp"
#include "FlashAir.hpp"
#include "AstroBox.hpp"
#include "Repetier.hpp"
#include "../GUI/PrintHostDialogs.hpp"

namespace fs = boost::filesystem;
using boost::optional;
using Slic3r::GUI::PrintHostQueueDialog;

namespace Slic3r {


PrintHost::~PrintHost() {}

PrintHost* PrintHost::get_print_host(DynamicPrintConfig *config)
{
    PrinterTechnology tech = ptFFF;

    {
        const auto opt = config->option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
        if (opt != nullptr) {
            tech = opt->value;
        }
    }

    if (tech == ptFFF) {
        const auto opt = config->option<ConfigOptionEnum<PrintHostType>>("host_type");
        const auto host_type = opt != nullptr ? opt->value : htOctoPrint;

        switch (host_type) {
            case htOctoPrint: return new OctoPrint(config);
            case htDuet:      return new Duet(config);
            case htFlashAir:  return new FlashAir(config);
            case htAstroBox:  return new AstroBox(config);
            case htRepetier:  return new Repetier(config);
            case htPrusaLink: return new PrusaLink(config);
            default:          return nullptr;
        }
    } else {
        return new SL1Host(config);
    }
}

wxString PrintHost::format_error(const std::string &body, const std::string &error, unsigned status) const
{
    if (status != 0) {
        auto wxbody = wxString::FromUTF8(body.data());
        return wxString::Format("HTTP %u: %s", status, wxbody);
    } else {
        return wxString::FromUTF8(error.data());
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
    fs::path source_to_remove;

    std::thread bg_thread;
    bool bg_exit = false;

    PrintHostQueueDialog *queue_dialog;

    priv(PrintHostJobQueue *q) : q(q) {}

    void emit_progress(int progress);
    void emit_error(wxString error);
    void emit_cancel(size_t id);
    void start_bg_thread();
    void stop_bg_thread();
    void bg_thread_main();
    void progress_fn(Http::Progress progress, bool &cancel);
    void remove_source(const fs::path &path);
    void remove_source();
    void perform_job(PrintHostJob the_job);
};

PrintHostJobQueue::PrintHostJobQueue(PrintHostQueueDialog *queue_dialog)
    : p(new priv(this))
{
    p->queue_dialog = queue_dialog;
}

PrintHostJobQueue::~PrintHostJobQueue()
{
    if (p) { p->stop_bg_thread(); }
}

void PrintHostJobQueue::priv::emit_progress(int progress)
{
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_PROGRESS, queue_dialog->GetId(), job_id, progress);
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::emit_error(wxString error)
{
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_ERROR, queue_dialog->GetId(), job_id, std::move(error));
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::emit_cancel(size_t id)
{
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_CANCEL, queue_dialog->GetId(), id);
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::start_bg_thread()
{
    if (bg_thread.joinable()) { return; }

    std::shared_ptr<priv> p2 = q->p;
    bg_thread = std::thread([p2]() {
        p2->bg_thread_main();
    });
}

void PrintHostJobQueue::priv::stop_bg_thread()
{
    if (bg_thread.joinable()) {
        bg_exit = true;
        channel_jobs.push(PrintHostJob()); // Push an empty job to wake up bg_thread in case it's sleeping
        bg_thread.detach();                // Let the background thread go, it should exit on its own
    }
}

void PrintHostJobQueue::priv::bg_thread_main()
{
    // bg thread entry point

    try {
        // Pick up jobs from the job channel:
        while (! bg_exit) {
            auto job = channel_jobs.pop();   // Sleeps in a cond var if there are no jobs
            if (job.empty()) {
                // This happens when the thread is being stopped
                break;
            }

            source_to_remove = job.upload_data.source_path;

            BOOST_LOG_TRIVIAL(debug) << boost::format("PrintHostJobQueue/bg_thread: Received job: [%1%]: `%2%` -> `%3%`, cancelled: %4%")
                % job_id
                % job.upload_data.upload_path
                % job.printhost->get_host()
                % job.cancelled;

            if (! job.cancelled) {
                perform_job(std::move(job));
            }

            remove_source();
            job_id++;
        }
    } catch (const std::exception &e) {
        emit_error(e.what());
    }

    // Cleanup leftover files, if any
    remove_source();
    auto jobs = channel_jobs.lock_rw();
    for (const PrintHostJob &job : *jobs) {
        remove_source(job.upload_data.source_path);
    }
}

void PrintHostJobQueue::priv::progress_fn(Http::Progress progress, bool &cancel)
{
    if (cancel) {
        // When cancel is true from the start, Http indicates request has been cancelled
        emit_cancel(job_id);
        return;
    }

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
                const size_t idx = cancel_id - job_id - 1;
                if (idx < jobs->size()) {
                    jobs->at(idx).cancelled = true;
                    BOOST_LOG_TRIVIAL(debug) << boost::format("PrintHostJobQueue: Job id %1% cancelled") % cancel_id;
                    emit_cancel(cancel_id);
                }
            }
        }

        cancels->clear();
    }

    if (! cancel) {
        int gui_progress = progress.ultotal > 0 ? 100*progress.ulnow / progress.ultotal : 0;
        if (gui_progress != prev_progress) {
            emit_progress(gui_progress);
            prev_progress = gui_progress;
        }
    }
}

void PrintHostJobQueue::priv::remove_source(const fs::path &path)
{
    if (! path.empty()) {
        boost::system::error_code ec;
        fs::remove(path, ec);
        if (ec) {
            BOOST_LOG_TRIVIAL(error) << boost::format("PrintHostJobQueue: Error removing file `%1%`: %2%") % path % ec;
        }
    }
}

void PrintHostJobQueue::priv::remove_source()
{
    remove_source(source_to_remove);
    source_to_remove.clear();
}

void PrintHostJobQueue::priv::perform_job(PrintHostJob the_job)
{
    emit_progress(0);   // Indicate the upload is starting

    bool success = the_job.printhost->upload(std::move(the_job.upload_data),
        [this](Http::Progress progress, bool &cancel) { this->progress_fn(std::move(progress), cancel); },
        [this](wxString error) {
            emit_error(std::move(error));
        }
    );

    if (success) {
        emit_progress(100);
    }
}

void PrintHostJobQueue::enqueue(PrintHostJob job)
{
    p->start_bg_thread();
    p->queue_dialog->append_job(job);
    p->channel_jobs.push(std::move(job));
}

void PrintHostJobQueue::cancel(size_t id)
{
    p->channel_cancels.push(id);
}

}
