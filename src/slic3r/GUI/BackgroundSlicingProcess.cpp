#include "BackgroundSlicingProcess.hpp"
#include "GUI_App.hpp"

#include <wx/event.h>
#include <wx/panel.h>
#include <wx/stdpaths.h>

// Print now includes tbb, and tbb includes Windows. This breaks compilation of wxWidgets if included before wx.
#include "../../libslic3r/Print.hpp"
#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/GCode/PostProcessor.hpp"

//#undef NDEBUG
#include <cassert>
#include <stdexcept>

#include <boost/format.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/cstdio.hpp>

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

void BackgroundSlicingProcess::thread_proc()
{
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
		    m_print->process();
		    if (! m_print->canceled()) {
                wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, new wxCommandEvent(m_event_sliced_id));
			    m_print->export_gcode(m_temp_output_path, m_gcode_preview_data);
			    if (! m_print->canceled() && ! this->is_step_done(bspsGCodeFinalize)) {
			    	this->set_step_started(bspsGCodeFinalize);
			    	if (! m_export_path.empty()) {
			    		//FIXME localize the messages
				    	if (copy_file(m_temp_output_path, m_export_path) != 0)
			    			throw std::runtime_error("Copying of the temporary G-code to the output G-code failed");
			    		m_print->set_status(95, "Running post-processing scripts");
			    		run_post_process_scripts(m_export_path, m_print->config());
			    		m_print->set_status(100, "G-code file exported to " + m_export_path);
			    	} else {
			    		m_print->set_status(100, "Slicing complete");
			    	}
					this->set_step_done(bspsGCodeFinalize);
			    }
		    }
		} catch (CanceledException &ex) {
			// Canceled, this is all right.
			assert(m_print->canceled());
		} catch (std::exception &ex) {
			error = ex.what();
		} catch (...) {
			error = "Unknown C++ exception.";
		}
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
	std::unique_lock<std::mutex> lck(m_mutex);
	if (m_state == STATE_INITIAL) {
		// The worker thread is not running yet. Start it.
		assert(! m_thread.joinable());
		m_thread = std::thread([this]{this->thread_proc();});
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

// To be called by Print::apply() through the Print::m_cancel_callback to stop the background
// processing before changing any data of running or finalized milestones.
// This function shall not trigger any UI update through the wxWidgets event.
void BackgroundSlicingProcess::stop_internal()
{
	std::unique_lock<std::mutex> lck(m_mutex);
	assert(m_state == STATE_STARTED || m_state == STATE_RUNNING || m_state == STATE_FINISHED || m_state == STATE_CANCELED);
	if (m_state == STATE_STARTED || m_state == STATE_RUNNING) {
		m_print->cancel_internal();
		// Wait until the background processing stops by being canceled.
		m_condition.wait(lck, [this](){ return m_state == STATE_CANCELED; });
	}
	// In the "Canceled" state. Reset the state to "Idle".
	m_state = STATE_IDLE;
	m_print->set_cancel_callback([](){});
}

// Apply config over the print. Returns false, if the new config values caused any of the already
// processed steps to be invalidated, therefore the task will need to be restarted.
bool BackgroundSlicingProcess::apply_config(const DynamicPrintConfig &config)
{
	this->stop();
	bool invalidated = m_print->apply_config(config);
	return invalidated;
}

// Apply config over the print. Returns false, if the new config values caused any of the already
// processed steps to be invalidated, therefore the task will need to be restarted.
Print::ApplyStatus BackgroundSlicingProcess::apply(const Model &model, const DynamicPrintConfig &config)
{
//	this->stop();
	Print::ApplyStatus invalidated = m_print->apply(model, config);
	return invalidated;
}

// Set the output path of the G-code.
void BackgroundSlicingProcess::schedule_export(const std::string &path)
{ 
	assert(m_export_path.empty());
	if (! m_export_path.empty())
		return;

	// Guard against entering the export step before changing the export path.
	tbb::mutex::scoped_lock lock(m_step_state_mutex);
	this->invalidate_step(bspsGCodeFinalize);
	m_export_path = path;
}

void BackgroundSlicingProcess::reset_export()
{
	assert(! this->running());
	if (! this->running()) {
		m_export_path.clear();
		// invalidate_step expects the mutex to be locked.
		tbb::mutex::scoped_lock lock(m_step_state_mutex);
		this->invalidate_step(bspsGCodeFinalize);
	}
}

void BackgroundSlicingProcess::set_step_started(BackgroundSlicingProcessStep step)
{ 
	m_step_state.set_started(step, m_step_state_mutex);
	if (m_print->canceled())
		throw CanceledException();
}

void BackgroundSlicingProcess::set_step_done(BackgroundSlicingProcessStep step)
{ 
	m_step_state.set_done(step, m_step_state_mutex);
	if (m_print->canceled())
		throw CanceledException();
}

bool BackgroundSlicingProcess::invalidate_step(BackgroundSlicingProcessStep step)
{
    bool invalidated = m_step_state.invalidate(step, m_step_state_mutex, [this](){ this->stop(); });
    return invalidated;
}

bool BackgroundSlicingProcess::invalidate_all_steps()
{ 
	return m_step_state.invalidate_all(m_step_state_mutex, [this](){ this->stop(); });
}

}; // namespace Slic3r
