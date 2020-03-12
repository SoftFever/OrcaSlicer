#ifndef slic3r_GUI_BackgroundSlicingProcess_hpp_
#define slic3r_GUI_BackgroundSlicingProcess_hpp_

#include <string>
#include <condition_variable>
#include <mutex>

#include <boost/filesystem.hpp>

#include <wx/event.h>

#include "libslic3r/Print.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "slic3r/Utils/Thread.hpp"

namespace Slic3r {

class DynamicPrintConfig;
class GCodePreviewData;
class Model;
class SLAPrint;

class SlicingStatusEvent : public wxEvent
{
public:
	SlicingStatusEvent(wxEventType eventType, int winid, const PrintBase::SlicingStatus &status) :
		wxEvent(winid, eventType), status(std::move(status)) {}
	virtual wxEvent *Clone() const { return new SlicingStatusEvent(*this); }

	PrintBase::SlicingStatus status;
};

wxDEFINE_EVENT(EVT_SLICING_UPDATE, SlicingStatusEvent);

// Print step IDs for keeping track of the print state.
enum BackgroundSlicingProcessStep {
    bspsGCodeFinalize, bspsCount,
};

// Support for the GUI background processing (Slicing and G-code generation).
// As of now this class is not declared in Slic3r::GUI due to the Perl bindings limits.
class BackgroundSlicingProcess
{
public:
	BackgroundSlicingProcess();
	// Stop the background processing and finalize the bacgkround processing thread, remove temp files.
	~BackgroundSlicingProcess();

	void set_fff_print(Print *print) { m_fff_print = print; }
	void set_sla_print(SLAPrint *print) { m_sla_print = print; }
	void set_gcode_preview_data(GCodePreviewData *gpd) { m_gcode_preview_data = gpd; }
#if ENABLE_THUMBNAIL_GENERATOR
    void set_thumbnail_cb(ThumbnailsGeneratorCallback cb) { m_thumbnail_cb = cb; }
#endif // ENABLE_THUMBNAIL_GENERATOR

	// The following wxCommandEvent will be sent to the UI thread / Plater window, when the slicing is finished
	// and the background processing will transition into G-code export.
	// The wxCommandEvent is sent to the UI thread asynchronously without waiting for the event to be processed.
	void set_slicing_completed_event(int event_id) { m_event_slicing_completed_id = event_id; }
	// The following wxCommandEvent will be sent to the UI thread / Plater window, when the G-code export is finished.
	// The wxCommandEvent is sent to the UI thread asynchronously without waiting for the event to be processed.
	void set_finished_event(int event_id) { m_event_finished_id = event_id; }

	// Activate either m_fff_print or m_sla_print.
	// Return true if changed.
	bool select_technology(PrinterTechnology tech);

	// Get the currently active printer technology.
	PrinterTechnology   current_printer_technology() const;
	// Get the current print. It is either m_fff_print or m_sla_print.
	const PrintBase*    current_print() const { return m_print; }
	const Print* 		fff_print() const { return m_fff_print; }
	const SLAPrint* 	sla_print() const { return m_sla_print; }
    // Take the project path (if provided), extract the name of the project, run it through the macro processor and save it next to the project file.
    // If the project_path is empty, just run output_filepath().
	std::string 		output_filepath_for_project(const boost::filesystem::path &project_path);

	// Start the background processing. Returns false if the background processing was already running.
	bool start();
	// Cancel the background processing. Returns false if the background processing was not running.
	// A stopped background processing may be restarted with start().
	bool stop();
	// Cancel the background processing and reset the print. Returns false if the background processing was not running.
	// Useful when the Model or configuration is being changed drastically.
	bool reset();

	// Apply config over the print. Returns false, if the new config values caused any of the already
	// processed steps to be invalidated, therefore the task will need to be restarted.
	Print::ApplyStatus apply(const Model &model, const DynamicPrintConfig &config);
	// After calling the apply() function, set_task() may be called to limit the task to be processed by process().
	// This is useful for calculating SLA supports for a single object only.
	void 		set_task(const PrintBase::TaskParams &params);
	// After calling apply, the empty() call will report whether there is anything to slice.
	bool 		empty() const;
	// Validate the print. Returns an empty string if valid, returns an error message if invalid.
	// Call validate before calling start().
	std::string validate();

	// Set the export path of the G-code.
	// Once the path is set, the G-code 
	void schedule_export(const std::string &path, bool export_path_on_removable_media);
	// Set print host upload job data to be enqueued to the PrintHostJobQueue
	// after current print slicing is complete
	void schedule_upload(Slic3r::PrintHostJob upload_job);
	// Clear m_export_path.
	void reset_export();
	// Once the G-code export is scheduled, the apply() methods will do nothing.
	bool is_export_scheduled() const { return ! m_export_path.empty(); }
	bool is_upload_scheduled() const { return ! m_upload_job.empty(); }

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
    // Returns true if the last step of the active print was finished with success.
    // The "finished" flag is reset by the apply() method, if it changes the state of the print.
    // This "finished" flag does not account for the final export of the output file (.gcode or zipped PNGs),
    // and it does not account for the OctoPrint scheduling.
    bool    finished() const { return m_print->finished(); }
    
private:
	void 	thread_proc();
	void 	thread_proc_safe();
	void 	join_background_thread();
	// To be called by Print::apply() through the Print::m_cancel_callback to stop the background
	// processing before changing any data of running or finalized milestones.
	// This function shall not trigger any UI update through the wxWidgets event.
	void	stop_internal();

	// Helper to wrap the FFF slicing & G-code generation.
	void	process_fff();

    // Temporary: for mimicking the fff file export behavior with the raster output
    void	process_sla();

	// Currently active print. It is one of m_fff_print and m_sla_print.
	PrintBase				   *m_print 			 = nullptr;
	// Non-owned pointers to Print instances.
	Print 					   *m_fff_print 		 = nullptr;
	SLAPrint 				   *m_sla_print			 = nullptr;
	// Data structure, to which the G-code export writes its annotations.
	GCodePreviewData 		   *m_gcode_preview_data = nullptr;
#if ENABLE_THUMBNAIL_GENERATOR
    // Callback function, used to write thumbnails into gcode.
    ThumbnailsGeneratorCallback m_thumbnail_cb 		 = nullptr;
#endif // ENABLE_THUMBNAIL_GENERATOR
	// Temporary G-code, there is one defined for the BackgroundSlicingProcess, differentiated from the other processes by a process ID.
	std::string 				m_temp_output_path;
	// Output path provided by the user. The output path may be set even if the slicing is running,
	// but once set, it cannot be re-set.
	std::string 				m_export_path;
	bool 						m_export_path_on_removable_media = false;
	// Print host upload job to schedule after slicing is complete, used by schedule_upload(),
	// empty by default (ie. no upload to schedule)
	PrintHostJob                m_upload_job;
	// Thread, on which the background processing is executed. The thread will always be present
	// and ready to execute the slicing process.
	boost::thread		 		m_thread;
	// Mutex and condition variable to synchronize m_thread with the UI thread.
	std::mutex 		 			m_mutex;
	std::condition_variable		m_condition;
	State 						m_state = STATE_INITIAL;

    PrintState<BackgroundSlicingProcessStep, bspsCount>   	m_step_state;
    mutable tbb::mutex                      				m_step_state_mutex;
	bool                set_step_started(BackgroundSlicingProcessStep step);
	void                set_step_done(BackgroundSlicingProcessStep step);
	bool 				is_step_done(BackgroundSlicingProcessStep step) const;
	bool                invalidate_step(BackgroundSlicingProcessStep step);
    bool                invalidate_all_steps();
    // If the background processing stop was requested, throw CanceledException.
    void                throw_if_canceled() const { if (m_print->canceled()) throw CanceledException(); }
    void                prepare_upload();

	// wxWidgets command ID to be sent to the plater to inform that the slicing is finished, and the G-code export will continue.
	int 						m_event_slicing_completed_id 	= 0;
	// wxWidgets command ID to be sent to the plater to inform that the task finished.
	int 						m_event_finished_id  			= 0;
};

}; // namespace Slic3r

#endif /* slic3r_GUI_BackgroundSlicingProcess_hpp_ */
