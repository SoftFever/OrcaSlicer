#ifndef slic3r_GUI_BackgroundSlicingProcess_hpp_
#define slic3r_GUI_BackgroundSlicingProcess_hpp_

#include <string>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Slic3r {

class DynamicPrintConfig;
class GCodePreviewData;
class Print;

// Support for the GUI background processing (Slicing and G-code generation).
// As of now this class is not declared in Slic3r::GUI due to the Perl bindings limits.
class BackgroundSlicingProcess
{
public:
	BackgroundSlicingProcess() {}
	~BackgroundSlicingProcess() { this->stop(); this->join_background_thread(); }

	void set_print(Print *print) { m_print = print; }
	void set_gcode_preview_data(GCodePreviewData *gpd) { m_gcode_preview_data = gpd; }
	void set_sliced_event(int event_id) { m_event_sliced_id = event_id; }
	void set_finished_event(int event_id) { m_event_finished_id = event_id; }

	// Start the background processing. Returns false if the background processing was already running.
	bool start();
	// Cancel the background processing. Returns false if the background processing was not running.
	// A stopped background processing may be restarted with start().
	bool stop();

	// Apply config over the print. Returns false, if the new config values caused any of the already
	// processed steps to be invalidated, therefore the task will need to be restarted.
	bool apply_config(DynamicPrintConfig *config);

	enum State {
		// m_thread  is not running yet, or it did not reach the STATE_IDLE yet (it does not wait on the condition yet).
		STATE_INITIAL = 0,
		// m_thread is waiting for the task to execute.
		STATE_IDLE,
		STATE_STARTED,
		// m_thread is executing a task.
		STATE_RUNNING,
		// m_thread finished executing a task, and it is waiting until the UI thread picks up the results.
		STATE_FINISHED,
		// m_thread finished executing a task, the task has been canceled by the UI thread, therefore the UI thread will not be notified.
		STATE_CANCELED,
		// m_thread exited the loop and it is going to finish. The UI thread should join on m_thread.
		STATE_EXIT,
		STATE_EXITED,
	};
	State 	state() 	const { return m_state; }
	bool    idle() 		const { return m_state == STATE_IDLE; }
	bool    running() 	const { return m_state == STATE_STARTED || m_state == STATE_RUNNING || m_state == STATE_FINISHED || m_state == STATE_CANCELED; }

private:
	void 	thread_proc();
	void 	start_background_thread();
	void 	join_background_thread();

	Print 					   *m_print 			 = nullptr;
	GCodePreviewData 		   *m_gcode_preview_data = nullptr;
	std::string 				m_output_path;
	// Thread, on which the background processing is executed. The thread will always be present
	// and ready to execute the slicing process.
	std::thread		 			m_thread;
	// Mutex and condition variable to synchronize m_thread with the UI thread.
	std::mutex 		 			m_mutex;
	std::condition_variable		m_condition;
	State 						m_state = STATE_INITIAL;

	// wxWidgets command ID to be sent to the platter to inform that the slicing is finished, and the G-code export will continue.
	int 						m_event_sliced_id 	 = 0;
	// wxWidgets command ID to be sent to the platter to inform that the task finished.
	int 						m_event_finished_id  = 0;
};

}; // namespace Slic3r

#endif /* slic3r_GUI_BackgroundSlicingProcess_hpp_ */
