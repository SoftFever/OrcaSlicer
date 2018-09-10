#include "BackgroundSlicingProcess.hpp"
#include "GUI.hpp"

#include "../../libslic3r/Print.hpp"

#include <wx/event.h>
#include <wx/panel.h>

//#undef NDEBUG
#include <cassert>
#include <stdexcept>

namespace Slic3r {

namespace GUI {
	extern wxPanel *g_wxPlater;
};

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
				wxQueueEvent(GUI::g_wxPlater, new wxCommandEvent(m_event_sliced_id));
			    m_print->export_gcode(m_output_path, m_gcode_preview_data);
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
		wxCommandEvent evt(m_event_finished_id);
		evt.SetString(error);
		evt.SetInt(m_print->canceled() ? -1 : (error.empty() ? 1 : 0));
	    wxQueueEvent(GUI::g_wxPlater, evt.Clone());
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
	lck.unlock();
	m_condition.notify_one();
	return true;
}

bool BackgroundSlicingProcess::stop()
{
	std::unique_lock<std::mutex> lck(m_mutex);
	if (m_state == STATE_INITIAL)
		return false;
	assert(this->running());
	if (m_state == STATE_STARTED || m_state == STATE_RUNNING) {
		m_print->cancel();
		// Wait until the background processing stops by being canceled.
		m_condition.wait(lck, [this](){ return m_state == STATE_CANCELED; });
		// In the "Canceled" state. Reset the state to "Idle".
		m_state = STATE_IDLE;
	} else if (m_state == STATE_FINISHED || m_state == STATE_CANCELED) {
		// In the "Finished" or "Canceled" state. Reset the state to "Idle".
		m_state = STATE_IDLE;
	}
	return true;
}

// Apply config over the print. Returns false, if the new config values caused any of the already
// processed steps to be invalidated, therefore the task will need to be restarted.
bool BackgroundSlicingProcess::apply_config(const DynamicPrintConfig &config)
{
	this->stop();
	bool invalidated = m_print->apply_config(config);
	return invalidated;
}

}; // namespace Slic3r
