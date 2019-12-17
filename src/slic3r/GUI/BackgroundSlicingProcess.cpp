#include "BackgroundSlicingProcess.hpp"
#include "GUI_App.hpp"

#include <wx/app.h>
#include <wx/panel.h>
#include <wx/stdpaths.h>

// For zipped archive creation
#include <wx/stdstream.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#if ENABLE_THUMBNAIL_GENERATOR
#include <miniz.h>
#endif // ENABLE_THUMBNAIL_GENERATOR

// Print now includes tbb, and tbb includes Windows. This breaks compilation of wxWidgets if included before wx.
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/GCode/PostProcessor.hpp"
#include "libslic3r/GCode/PreviewData.hpp"
#include "libslic3r/libslic3r.h"

#include <cassert>
#include <stdexcept>
#include <cctype>
#include <algorithm>

#include <boost/format.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>
#include "I18N.hpp"
#include "GUI.hpp"

namespace Slic3r {

BackgroundSlicingProcess::BackgroundSlicingProcess()
{
    boost::filesystem::path temp_path(wxStandardPaths::Get().GetTempDir().utf8_str().data());
    temp_path /= (boost::format(".%1%.gcode") % get_current_pid()).str();
	m_temp_output_path = temp_path.string();
}

BackgroundSlicingProcess::~BackgroundSlicingProcess() 
{ 
	this->stop();
	this->join_background_thread();
	boost::nowide::remove(m_temp_output_path.c_str());
}

bool BackgroundSlicingProcess::select_technology(PrinterTechnology tech)
{
	bool changed = false;
	if (m_print == nullptr || m_print->technology() != tech) {
		if (m_print != nullptr)
			this->reset();
		switch (tech) {
		case ptFFF: m_print = m_fff_print; break;
		case ptSLA: m_print = m_sla_print; break;
        default: assert(false); break;
		}
		changed = true;
	}
	assert(m_print != nullptr);
	return changed;
}

PrinterTechnology BackgroundSlicingProcess::current_printer_technology() const
{
	return m_print->technology();
}

std::string BackgroundSlicingProcess::output_filepath_for_project(const boost::filesystem::path &project_path)
{
	assert(m_print != nullptr);
    if (project_path.empty())
        return m_print->output_filepath("");
    return m_print->output_filepath(project_path.parent_path().string(), project_path.stem().string());
}

// This function may one day be merged into the Print, but historically the print was separated
// from the G-code generator.
void BackgroundSlicingProcess::process_fff()
{
	assert(m_print == m_fff_print);
    m_print->process();
	wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, new wxCommandEvent(m_event_slicing_completed_id));
#if ENABLE_THUMBNAIL_GENERATOR
    m_fff_print->export_gcode(m_temp_output_path, m_gcode_preview_data, m_thumbnail_cb);
#else
    m_fff_print->export_gcode(m_temp_output_path, m_gcode_preview_data);
#endif // ENABLE_THUMBNAIL_GENERATOR

    /* #ys_FIXME_no_exported_codes
    if (m_fff_print->model().custom_gcode_per_print_z != GUI::wxGetApp().model().custom_gcode_per_print_z) {
        GUI::wxGetApp().model().custom_gcode_per_print_z = m_fff_print->model().custom_gcode_per_print_z;
        GUI::show_info(nullptr, _(L("To except of redundant tool manipulation, \n"
                                    "Color change(s) for unused extruder(s) was(were) deleted")), _(L("Info")));
    }
    */

	if (this->set_step_started(bspsGCodeFinalize)) {
	    if (! m_export_path.empty()) {
	    	//FIXME localize the messages
	    	// Perform the final post-processing of the export path by applying the print statistics over the file name.
	    	std::string export_path = m_fff_print->print_statistics().finalize_output_path(m_export_path);
		    if (copy_file(m_temp_output_path, export_path) != 0)
	    		throw std::runtime_error(_utf8(L("Copying of the temporary G-code to the output G-code failed. Maybe the SD card is write locked?")));
	    	m_print->set_status(95, _utf8(L("Running post-processing scripts")));
	    	run_post_process_scripts(export_path, m_fff_print->config());
	    	m_print->set_status(100, (boost::format(_utf8(L("G-code file exported to %1%"))) % export_path).str());
	    } else if (! m_upload_job.empty()) {
			prepare_upload();
	    } else {
			m_print->set_status(100, _utf8(L("Slicing complete")));
	    }
		this->set_step_done(bspsGCodeFinalize);
	}
}

#if ENABLE_THUMBNAIL_GENERATOR
static void write_thumbnail(Zipper& zipper, const ThumbnailData& data)
{
    size_t png_size = 0;
    void* png_data = tdefl_write_image_to_png_file_in_memory_ex((const void*)data.pixels.data(), data.width, data.height, 4, &png_size, MZ_DEFAULT_LEVEL, 1);
    if (png_data != nullptr)
    {
        zipper.add_entry("thumbnail/thumbnail" + std::to_string(data.width) + "x" + std::to_string(data.height) + ".png", (const std::uint8_t*)png_data, png_size);
        mz_free(png_data);
    }
}
#endif // ENABLE_THUMBNAIL_GENERATOR

void BackgroundSlicingProcess::process_sla()
{
    assert(m_print == m_sla_print);
    m_print->process();
    if (this->set_step_started(bspsGCodeFinalize)) {
        if (! m_export_path.empty()) {
            const std::string export_path = m_sla_print->print_statistics().finalize_output_path(m_export_path);

            Zipper zipper(export_path);
            m_sla_print->export_raster(zipper);

#if ENABLE_THUMBNAIL_GENERATOR
            if (m_thumbnail_cb != nullptr)
            {
                ThumbnailsList thumbnails;
                m_thumbnail_cb(thumbnails, current_print()->full_print_config().option<ConfigOptionPoints>("thumbnails")->values, true, true, true, true);
//                m_thumbnail_cb(thumbnails, current_print()->full_print_config().option<ConfigOptionPoints>("thumbnails")->values, true, false, true, true); // renders also supports and pad
                for (const ThumbnailData& data : thumbnails)
                {
                    if (data.is_valid())
                        write_thumbnail(zipper, data);
                }
            }
#endif // ENABLE_THUMBNAIL_GENERATOR

            zipper.finalize();

            m_print->set_status(100, (boost::format(_utf8(L("Masked SLA file exported to %1%"))) % export_path).str());
        } else if (! m_upload_job.empty()) {
            prepare_upload();
        } else {
			m_print->set_status(100, _utf8(L("Slicing complete")));
        }
        this->set_step_done(bspsGCodeFinalize);
    }
}

void BackgroundSlicingProcess::thread_proc()
{
	assert(m_print != nullptr);
	assert(m_print == m_fff_print || m_print == m_sla_print);
	std::unique_lock<std::mutex> lck(m_mutex);
	// Let the caller know we are ready to run the background processing task.
	m_state = STATE_IDLE;
	lck.unlock();
	m_condition.notify_one();
	for (;;) {
		assert(m_state == STATE_IDLE || m_state == STATE_CANCELED || m_state == STATE_FINISHED);
		// Wait until a new task is ready to be executed, or this thread should be finished.
		lck.lock();
		m_condition.wait(lck, [this](){ return m_state == STATE_STARTED || m_state == STATE_EXIT; });
		if (m_state == STATE_EXIT)
			// Exiting this thread.
			break;
		// Process the background slicing task.
		m_state = STATE_RUNNING;
		lck.unlock();
		std::string error;
		try {
			assert(m_print != nullptr);
			switch(m_print->technology()) {
				case ptFFF: this->process_fff(); break;
                case ptSLA: this->process_sla(); break;
				default: m_print->process(); break;
			}
		} catch (CanceledException & /* ex */) {
			// Canceled, this is all right.
			assert(m_print->canceled());
        } catch (const std::bad_alloc& ex) {
            wxString errmsg = wxString::Format(_(L("%s has encountered an error. It was likely caused by running out of memory. "
                                  "If you are sure you have enough RAM on your system, this may also be a bug and we would "
                                  "be glad if you reported it.")), SLIC3R_APP_NAME);
            error = errmsg.ToStdString() + "\n\n" + std::string(ex.what());
        } catch (std::exception &ex) {
			error = ex.what();
		} catch (...) {
			error = "Unknown C++ exception.";
		}
		m_print->finalize();
		lck.lock();
		m_state = m_print->canceled() ? STATE_CANCELED : STATE_FINISHED;
		if (m_print->cancel_status() != Print::CANCELED_INTERNAL) {
			// Only post the canceled event, if canceled by user.
			// Don't post the canceled event, if canceled from Print::apply().
			wxCommandEvent evt(m_event_finished_id);
			evt.SetString(error);
			evt.SetInt(m_print->canceled() ? -1 : (error.empty() ? 1 : 0));
        	wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, evt.Clone());
        }
	    m_print->restart();
		lck.unlock();
		// Let the UI thread wake up if it is waiting for the background task to finish.
	    m_condition.notify_one();
	    // Let the UI thread see the result.
	}
	m_state = STATE_EXITED;
	lck.unlock();
	// End of the background processing thread. The UI thread should join m_thread now.
}

void BackgroundSlicingProcess::thread_proc_safe()
{
	try {
		this->thread_proc();
	} catch (...) {
		wxTheApp->OnUnhandledException();
   	}
}

void BackgroundSlicingProcess::join_background_thread()
{
	std::unique_lock<std::mutex> lck(m_mutex);
	if (m_state == STATE_INITIAL) {
		// Worker thread has not been started yet.
		assert(! m_thread.joinable());
	} else {
		assert(m_state == STATE_IDLE);
		assert(m_thread.joinable());
		// Notify the worker thread to exit.
		m_state = STATE_EXIT;
		lck.unlock();
		m_condition.notify_one();
		// Wait until the worker thread exits.
		m_thread.join();
	}
}

bool BackgroundSlicingProcess::start()
{
	if (m_print->empty())
		// The print is empty (no object in Model, or all objects are out of the print bed).
		return false;

	std::unique_lock<std::mutex> lck(m_mutex);
	if (m_state == STATE_INITIAL) {
		// The worker thread is not running yet. Start it.
		assert(! m_thread.joinable());
		m_thread = create_thread([this]{this->thread_proc_safe();});
		// Wait until the worker thread is ready to execute the background processing task.
		m_condition.wait(lck, [this](){ return m_state == STATE_IDLE; });
	}
	assert(m_state == STATE_IDLE || this->running());
	if (this->running())
		// The background processing thread is already running.
		return false;
	if (! this->idle())
		throw std::runtime_error("Cannot start a background task, the worker thread is not idle.");
	m_state = STATE_STARTED;
	m_print->set_cancel_callback([this](){ this->stop_internal(); });
	lck.unlock();
	m_condition.notify_one();
	return true;
}

bool BackgroundSlicingProcess::stop()
{
	// m_print->state_mutex() shall NOT be held. Unfortunately there is no interface to test for it.
	std::unique_lock<std::mutex> lck(m_mutex);
	if (m_state == STATE_INITIAL) {
//		this->m_export_path.clear();
		return false;
	}
//	assert(this->running());
	if (m_state == STATE_STARTED || m_state == STATE_RUNNING) {
		m_print->cancel();
		// Wait until the background processing stops by being canceled.
		m_condition.wait(lck, [this](){ return m_state == STATE_CANCELED; });
		// In the "Canceled" state. Reset the state to "Idle".
		m_state = STATE_IDLE;
		m_print->set_cancel_callback([](){});
	} else if (m_state == STATE_FINISHED || m_state == STATE_CANCELED) {
		// In the "Finished" or "Canceled" state. Reset the state to "Idle".
		m_state = STATE_IDLE;
		m_print->set_cancel_callback([](){});
	}
//	this->m_export_path.clear();
	return true;
}

bool BackgroundSlicingProcess::reset()
{
	bool stopped = this->stop();
	this->reset_export();
	m_print->clear();
	this->invalidate_all_steps();
	return stopped;
}

// To be called by Print::apply() through the Print::m_cancel_callback to stop the background
// processing before changing any data of running or finalized milestones.
// This function shall not trigger any UI update through the wxWidgets event.
void BackgroundSlicingProcess::stop_internal()
{
	// m_print->state_mutex() shall be held. Unfortunately there is no interface to test for it.
	if (m_state == STATE_IDLE)
		// The worker thread is waiting on m_mutex/m_condition for wake up. The following lock of the mutex would block.
		return;
	std::unique_lock<std::mutex> lck(m_mutex);
	assert(m_state == STATE_STARTED || m_state == STATE_RUNNING || m_state == STATE_FINISHED || m_state == STATE_CANCELED);
	if (m_state == STATE_STARTED || m_state == STATE_RUNNING) {
		// At this point of time the worker thread may be blocking on m_print->state_mutex().
		// Set the print state to canceled before unlocking the state_mutex(), so when the worker thread wakes up,
		// it throws the CanceledException().
		m_print->cancel_internal();
		// Allow the worker thread to wake up if blocking on a milestone.
		m_print->state_mutex().unlock();
		// Wait until the background processing stops by being canceled.
		m_condition.wait(lck, [this](){ return m_state == STATE_CANCELED; });
		// Lock it back to be in a consistent state.
		m_print->state_mutex().lock();
	}
	// In the "Canceled" state. Reset the state to "Idle".
	m_state = STATE_IDLE;
	m_print->set_cancel_callback([](){});
}

bool BackgroundSlicingProcess::empty() const
{
	assert(m_print != nullptr);
	return m_print->empty();
}

std::string BackgroundSlicingProcess::validate()
{
	assert(m_print != nullptr);
	return m_print->validate();
}

// Apply config over the print. Returns false, if the new config values caused any of the already
// processed steps to be invalidated, therefore the task will need to be restarted.
Print::ApplyStatus BackgroundSlicingProcess::apply(const Model &model, const DynamicPrintConfig &config)
{
	assert(m_print != nullptr);
	assert(config.opt_enum<PrinterTechnology>("printer_technology") == m_print->technology());
	Print::ApplyStatus invalidated = m_print->apply(model, config);
	if ((invalidated & PrintBase::APPLY_STATUS_INVALIDATED) != 0 && m_print->technology() == ptFFF &&
		m_gcode_preview_data != nullptr && ! this->m_fff_print->is_step_done(psGCodeExport)) {
		// Some FFF status was invalidated, and the G-code was not exported yet.
		// Let the G-code preview UI know that the final G-code preview is not valid.
		// In addition, this early memory deallocation reduces memory footprint.
		m_gcode_preview_data->reset();
	}
	return invalidated;
}

void BackgroundSlicingProcess::set_task(const PrintBase::TaskParams &params)
{
	assert(m_print != nullptr);
	m_print->set_task(params);
}

// Set the output path of the G-code.
void BackgroundSlicingProcess::schedule_export(const std::string &path)
{ 
	assert(m_export_path.empty());
	if (! m_export_path.empty())
		return;

	// Guard against entering the export step before changing the export path.
	tbb::mutex::scoped_lock lock(m_print->state_mutex());
	this->invalidate_step(bspsGCodeFinalize);
	m_export_path = path;
}

void BackgroundSlicingProcess::schedule_upload(Slic3r::PrintHostJob upload_job)
{
	assert(m_export_path.empty());
	if (! m_export_path.empty())
		return;

	// Guard against entering the export step before changing the export path.
	tbb::mutex::scoped_lock lock(m_print->state_mutex());
	this->invalidate_step(bspsGCodeFinalize);
	m_export_path.clear();
	m_upload_job = std::move(upload_job);
}

void BackgroundSlicingProcess::reset_export()
{
	assert(! this->running());
	if (! this->running()) {
		m_export_path.clear();
		// invalidate_step expects the mutex to be locked.
		tbb::mutex::scoped_lock lock(m_print->state_mutex());
		this->invalidate_step(bspsGCodeFinalize);
	}
}

bool BackgroundSlicingProcess::set_step_started(BackgroundSlicingProcessStep step)
{ 
	return m_step_state.set_started(step, m_print->state_mutex(), [this](){ this->throw_if_canceled(); });
}

void BackgroundSlicingProcess::set_step_done(BackgroundSlicingProcessStep step)
{ 
	m_step_state.set_done(step, m_print->state_mutex(), [this](){ this->throw_if_canceled(); });
}

bool BackgroundSlicingProcess::is_step_done(BackgroundSlicingProcessStep step) const
{ 
	return m_step_state.is_done(step, m_print->state_mutex());
}

bool BackgroundSlicingProcess::invalidate_step(BackgroundSlicingProcessStep step)
{
    bool invalidated = m_step_state.invalidate(step, [this](){ this->stop_internal(); });
    return invalidated;
}

bool BackgroundSlicingProcess::invalidate_all_steps()
{ 
	return m_step_state.invalidate_all([this](){ this->stop_internal(); });
}

void BackgroundSlicingProcess::prepare_upload()
{
	// A print host upload job has been scheduled, enqueue it to the printhost job queue

	// XXX: is fs::path::string() right?

	// Generate a unique temp path to which the gcode/zip file is copied/exported
	boost::filesystem::path source_path = boost::filesystem::temp_directory_path()
		/ boost::filesystem::unique_path("." SLIC3R_APP_KEY ".upload.%%%%-%%%%-%%%%-%%%%");

	if (m_print == m_fff_print) {
		m_print->set_status(95, _utf8(L("Running post-processing scripts")));
		if (copy_file(m_temp_output_path, source_path.string()) != 0) {
			throw std::runtime_error(_utf8(L("Copying of the temporary G-code to the output G-code failed")));
		}
		run_post_process_scripts(source_path.string(), m_fff_print->config());
        m_upload_job.upload_data.upload_path = m_fff_print->print_statistics().finalize_output_path(m_upload_job.upload_data.upload_path.string());
    } else {
        m_upload_job.upload_data.upload_path = m_sla_print->print_statistics().finalize_output_path(m_upload_job.upload_data.upload_path.string());

        Zipper zipper{source_path.string()};
        m_sla_print->export_raster(zipper, m_upload_job.upload_data.upload_path.string());
#if ENABLE_THUMBNAIL_GENERATOR
        if (m_thumbnail_cb != nullptr)
        {
            ThumbnailsList thumbnails;
            m_thumbnail_cb(thumbnails, current_print()->full_print_config().option<ConfigOptionPoints>("thumbnails")->values, true, true, true, true);
//            m_thumbnail_cb(thumbnails, current_print()->full_print_config().option<ConfigOptionPoints>("thumbnails")->values, true, false, true, true); // renders also supports and pad
            for (const ThumbnailData& data : thumbnails)
            {
                if (data.is_valid())
                    write_thumbnail(zipper, data);
            }
        }
#endif // ENABLE_THUMBNAIL_GENERATOR
        zipper.finalize();
    }

    m_print->set_status(100, (boost::format(_utf8(L("Scheduling upload to `%1%`. See Window -> Print Host Upload Queue"))) % m_upload_job.printhost->get_host()).str());

	m_upload_job.upload_data.source_path = std::move(source_path);

	GUI::wxGetApp().printhost_job_queue().enqueue(std::move(m_upload_job));
}

}; // namespace Slic3r
