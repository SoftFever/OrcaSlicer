#ifndef slic3r_GUI_NotificationManager_hpp_
#define slic3r_GUI_NotificationManager_hpp_

#include "GUI_App.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"
#include "Event.hpp"
#include "I18N.hpp"
#include "Jobs/ProgressIndicator.hpp"

#include <libslic3r/ObjectID.hpp>
#include <libslic3r/Technologies.hpp>

#include <wx/time.h>

#include <string>
#include <vector>
#include <deque>
#include <unordered_set>

namespace Slic3r {
namespace GUI {

using EjectDriveNotificationClickedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_EJECT_DRIVE_NOTIFICAION_CLICKED, EjectDriveNotificationClickedEvent);
using ExportGcodeNotificationClickedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_EXPORT_GCODE_NOTIFICAION_CLICKED, ExportGcodeNotificationClickedEvent);
using PresetUpdateAvailableClickedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_PRESET_UPDATE_AVAILABLE_CLICKED, PresetUpdateAvailableClickedEvent);

using CancelFn = std::function<void()>;

class GLCanvas3D;
class ImGuiWrapper;
enum class InfoItemType;

enum class NotificationType
{
	CustomNotification,
//	SlicingNotPossible,
	// Notification on end of export to a removable media, with hyperling to eject the external media.
	// Obsolete by ExportFinished
//	ExportToRemovableFinished,
	// Notification on end of export, with hyperling to see folder and eject if export was to external media.
	// Own subclass.
	ExportFinished,
	// Works on OSX only.
	//FIXME Do we want to have it on Linux and Windows? Is it possible to get the Disconnect event on Windows?
	Mouse3dDisconnected,
//	Mouse3dConnected,
//	NewPresetsAviable,
	// Notification on the start of PrusaSlicer, when a new PrusaSlicer version is published.
	// Contains a hyperlink to open a web browser pointing to the PrusaSlicer download location.
	NewAppAvailable,
	// Like NewAppAvailable but with text and link for alpha / bet release
	NewAlphaAvailable,
	NewBetaAvailable,
	// Notification on the start of PrusaSlicer, when updates of system profiles are detected.
	// Contains a hyperlink to execute installation of the new system profiles.
	PresetUpdateAvailable,
//	LoadingFailed,
	// Errors emmited by Print::validate
	// difference from Slicing error is that they disappear not grey out at update_background_process
	ValidateError,
	// Notification emitted by Print::validate
	ValidateWarning,
	// Slicing error produced by BackgroundSlicingProcess::validate() or by the BackgroundSlicingProcess background
	// thread thowing a SlicingError exception.
	SlicingError,
	// Slicing warnings, issued by the slicing process.
	// Slicing warnings are registered for a particular Print milestone or a PrintObject and its milestone.
	SlicingWarning,
	// Object partially outside the print volume. Cannot slice.
	PlaterError,
	// Object fully outside the print volume, or extrusion outside the print volume. Slicing is not disabled.
	PlaterWarning,
	// Progress bar instead of text.
	ProgressBar,
	// Progress bar with info from Print Host Upload Queue dialog.
	PrintHostUpload,
	// Progress bar with cancel button, cannot be closed
	// On end of slicing and G-code processing (the full G-code preview is available),
	// contains a hyperlink to export the G-code to a removable media or hdd.
	SlicingProgress,
	// Notification, when Color Change G-code is empty and user try to add color change on DoubleSlider.
    EmptyColorChangeCode,
    // Notification that custom supports/seams were deleted after mesh repair.
    CustomSupportsAndSeamRemovedAfterRepair,
    // Notification that auto adding of color changes is impossible
    EmptyAutoColorChange,
    // Notification about detected sign
    SignDetected,
    // Notification telling user to quit SLA supports manual editing
    QuitSLAManualMode,
    // Desktop integration basic info
    DesktopIntegrationSuccess,
    DesktopIntegrationFail,
    UndoDesktopIntegrationSuccess,
    UndoDesktopIntegrationFail,
    // Notification that a printer has more extruders than are supported by MM Gizmo/segmentation.
    MmSegmentationExceededExtrudersLimit,
	// Did you know Notification appearing on startup with arrows to change hint
	DidYouKnowHint,
	// Shows when  ObjectList::update_info_items finds information that should be stressed to the user
	// Might contain logo taken from gizmos
	UpdatedItemsInfo,
	// Progress bar notification with methods to replace ProgressIndicator class.
	ProgressIndicator,
	// Give user advice to simplify object with big amount of triangles
	// Contains ObjectID for closing when object is deleted
	SimplifySuggestion,
	// information about netfabb is finished repairing model (blocking proccess)
	NetfabbFinished,
	// Short meesage to fill space between start and finish of export
	ExportOngoing
};

class NotificationManager
{
public:
	enum class NotificationLevel : int
	{
		// The notifications will be presented in the order of importance, thus these enum values
		// are sorted by the importance.
		// Important notification with progress bar, no fade-out, might appear again after closing. Position at the bottom.
		ProgressBarNotificationLevel = 1,
		// "Did you know" notification with special icon and buttons, Position close to bottom.
		HintNotificationLevel,
		// "Good to know" notification, usually but not always with a quick fade-out.		
		RegularNotificationLevel,
		// Regular level notifiaction containing info about objects or print. Has Icon.
		PrintInfoNotificationLevel,
		// PrintInfoNotificationLevel with shorter time
		PrintInfoShortNotificationLevel,
		// Information notification without a fade-out or with a longer fade-out.
		ImportantNotificationLevel,
		// Warning, no fade-out.
		WarningNotificationLevel,
		// Error, no fade-out. Top most position.
		ErrorNotificationLevel,
	};

	NotificationManager(wxEvtHandler* evt_handler);
	~NotificationManager(){}
	
	// init is called after canvas3d is created. Notifications added before init are not showed or updated
	void init() { m_initialized = true; }
	// Push a prefabricated notification from basic_notifications (see the table at the end of this file).
	void push_notification(const NotificationType type, int timestamp = 0);
	// Push a NotificationType::CustomNotification with NotificationLevel::RegularNotificationLevel and 10s fade out interval.
	void push_notification(const std::string& text, int timestamp = 0);
	// Push a NotificationType::CustomNotification with provided notification level and 10s for RegularNotificationLevel.
	// ErrorNotificationLevel and ImportantNotificationLevel are never faded out.
    void push_notification(NotificationType type, NotificationLevel level, const std::string& text, const std::string& hypertext = "",
                           std::function<bool(wxEvtHandler*)> callback = std::function<bool(wxEvtHandler*)>(), int timestamp = 0);
	// Pushes basic_notification with delay. See push_delayed_notification_data.
	void push_delayed_notification(const NotificationType type, std::function<bool(void)> condition_callback, int64_t initial_delay, int64_t delay_interval);
	// Removes all notifications of type from m_waiting_notifications
	void stop_delayed_notifications_of_type(const NotificationType type);
	// Creates Validate Error notification with a custom text and no fade out.
	void push_validate_error_notification(const std::string& text);
	// Creates Slicing Error notification with a custom text and no fade out.
	void push_slicing_error_notification(const std::string& text);
	// Creates Slicing Warning notification with a custom text and no fade out.
	void push_slicing_warning_notification(const std::string& text, bool gray, ObjectID oid, int warning_step);
	// marks slicing errors as gray
	void set_all_slicing_errors_gray(bool g);
	// marks slicing warings as gray
	void set_all_slicing_warnings_gray(bool g);
//	void set_slicing_warning_gray(const std::string& text, bool g);
	// immediately stops showing slicing errors
	void close_slicing_errors_and_warnings();
	void close_slicing_error_notification(const std::string& text);
	// Release those slicing warnings, which refer to an ObjectID, which is not in the list.
	// living_oids is expected to be sorted.
	void remove_slicing_warnings_of_released_objects(const std::vector<ObjectID>& living_oids);
	// Object partially outside of the printer working space, cannot print. No fade out.
	void push_plater_error_notification(const std::string& text);
	// Object fully out of the printer working space and such. No fade out.
	void push_plater_warning_notification(const std::string& text);
	// Closes error or warning of the same text
	void close_plater_error_notification(const std::string& text);
	void close_plater_warning_notification(const std::string& text);
	// Object warning with ObjectID, closes when object is deleted. ID used is of object not print like in slicing warning.
	void push_simplify_suggestion_notification(const std::string& text, ObjectID object_id, const std::string& hypertext = "",
		std::function<bool(wxEvtHandler*)> callback = std::function<bool(wxEvtHandler*)>());
	// Close object warnings, whose ObjectID is not in the list.
	// living_oids is expected to be sorted.
	void remove_simplify_suggestion_of_released_objects(const std::vector<ObjectID>& living_oids);
	void remove_simplify_suggestion_with_id(const ObjectID oid);
	// Called when the side bar changes its visibility, as the "slicing complete" notification supplements
	// the "slicing info" normally shown at the side bar.
	void set_sidebar_collapsed(bool collapsed);
	// Set technology for correct text in SlicingProgress.
	void set_fff(bool b);
	void set_fdm(bool b) { set_fff(b); }
	void set_sla(bool b) { set_fff(!b); }
	// Exporting finished, show this information with path, button to open containing folder and if ejectable - eject button
	void push_exporting_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable);
	// notifications with progress bar
	// print host upload
	void push_upload_job_notification(int id, float filesize, const std::string& filename, const std::string& host, float percentage = 0);
	void set_upload_job_notification_percentage(int id, const std::string& filename, const std::string& host, float percentage);
	void upload_job_notification_show_canceled(int id, const std::string& filename, const std::string& host);
	void upload_job_notification_show_error(int id, const std::string& filename, const std::string& host);
	// slicing progress
	void init_slicing_progress_notification(std::function<bool()> cancel_callback);
	void set_slicing_progress_began();
	// percentage negative = canceled, <0-1) = progress, 1 = completed 
	void set_slicing_progress_percentage(const std::string& text, float percentage);
	void set_slicing_progress_canceled(const std::string& text);
	// hides slicing progress notification imidietly
	void set_slicing_progress_hidden();
	// Add a print time estimate to an existing SlicingProgress notification. Set said notification to SP_COMPLETED state.
	void set_slicing_complete_print_time(const std::string& info, bool sidebar_colapsed);
	void set_slicing_progress_export_possible();
	// ProgressIndicator notification
	// init adds hidden instance of progress indi notif that should always live (goes to hidden instead of erasing)
	void init_progress_indicator();
	// functions equal to ProgressIndicator class
	void progress_indicator_set_range(int range);
	void progress_indicator_set_cancel_callback(CancelFn callback = CancelFn());
	void progress_indicator_set_progress(int pr);
	void progress_indicator_set_status_text(const char*); // utf8 char array
	int  progress_indicator_get_range() const;
	// Hint (did you know) notification
	void push_hint_notification(bool open_next);
	bool is_hint_notification_open();
	// Forces Hints to reload its content when next hint should be showed
	void deactivate_loaded_hints();
	// Adds counter to existing UpdatedItemsInfo notification or opens new one
	void push_updated_item_info_notification(InfoItemType type);
	// Close old notification ExportFinished.
	void new_export_began(bool on_removable);
	// finds ExportFinished notification and closes it if it was to removable device
	void device_ejected();
	// renders notifications in queue and deletes expired ones
	void render_notifications(GLCanvas3D& canvas, float overlay_width);
	// finds and closes all notifications of given type
	void close_notification_of_type(const NotificationType type);
	// Hides warnings in G-code preview. Should be called from plater only when 3d view/ preview is changed
    void set_in_preview(bool preview);
	// Calls set_in_preview to apply appearing or disappearing of some notificatons;
	void apply_in_preview() { set_in_preview(m_in_preview); }
	// Move to left to avoid colision with variable layer height gizmo.
	void set_move_from_overlay(bool move) { m_move_from_overlay = move; }
	// perform update_state on each notification and ask for more frames if needed, return true for render needed
	bool update_notifications(GLCanvas3D& canvas);
	// returns number of all notifications shown
	size_t get_notification_count() const;
private:
	// duration 0 means not disapearing
	struct NotificationData {
		NotificationType         type;
		NotificationLevel        level;
		// Fade out time
		const int                duration;
		const std::string        text1;
		const std::string        hypertext;
		// Callback for hypertext - returns true if notification should close after triggering
		// Usually sends event to UI thread thru wxEvtHandler.
		// Examples in basic_notifications.
        std::function<bool(wxEvtHandler*)> callback;
		const std::string        text2;
	};

	// Cache of IDs to identify and reuse ImGUI windows.
	class NotificationIDProvider
	{
	public:
		int 		allocate_id();
		void 		release_id(int id);

	private:
		// Next ID used for naming the ImGUI windows.
		int       			m_next_id{ 1 };
		// IDs of ImGUI windows, which were released and they are ready for reuse.
		std::vector<int>	m_released_ids;
	};

	//Pop notification - shows only once to user.
	class PopNotification
	{
	public:

		enum class EState
		{
			Unknown,		  // NOT initialized
			Hidden,
			Shown,			  // Requesting Render at some time if duration != 0
			NotFading,		  // Never jumps to state Fading out even if duration says so
			FadingOut,        // Requesting Render at some time
			ClosePending,     // Requesting Render
			Finished,         // Requesting Render
			Hovered,		  // Followed by Shown 
			Paused
		};

		PopNotification(const NotificationData &n, NotificationIDProvider &id_provider, wxEvtHandler* evt_handler);
		virtual ~PopNotification() { if (m_id) m_id_provider.release_id(m_id); }
		virtual void           render(GLCanvas3D& canvas, float initial_y, bool move_from_overlay, float overlay_width);
		// close will dissapear notification on next render
		virtual void           close() { m_state = EState::ClosePending; wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0);}
		// data from newer notification of same type
		void                   update(const NotificationData& n);
		bool                   is_finished() const { return m_state == EState::ClosePending || m_state == EState::Finished; }
		// returns top after movement
		float                  get_top() const { return m_top_y; }
		//returns top in actual frame
		float                  get_current_top() const { return m_top_y; }
		const NotificationType get_type() const { return m_data.type; }
		const NotificationData& get_data() const { return m_data; }
		const bool             is_gray() const { return m_is_gray; }
		void                   set_gray(bool g) { m_is_gray = g; }
		virtual bool           compare_text(const std::string& text) const;
        void                   hide(bool h) { if (is_finished()) return; m_state = h ? EState::Hidden : EState::Unknown; }
		// sets m_next_render with time of next mandatory rendering. Delta is time since last render.
		virtual bool           update_state(bool paused, const int64_t delta);
		int64_t 		       next_render() const { return is_finished() ? 0 : m_next_render; }
		EState                 get_state()  const { return m_state; }
		bool				   is_hovered() const { return m_state == EState::Hovered; } 
		void				   set_hovered() { if (m_state != EState::Finished && m_state != EState::ClosePending && m_state != EState::Hidden && m_state != EState::Unknown) m_state = EState::Hovered; }
		// set start of notification to now. Used by delayed notifications
		void                   reset_timer() { m_notification_start = GLCanvas3D::timestamp_now(); m_state = EState::Shown; }
	protected:
		// Call after every size change
		virtual void init();
		// Calculetes correct size but not se it in imgui!
		virtual void set_next_window_size(ImGuiWrapper& imgui);
		virtual void render_text(ImGuiWrapper& imgui,
			                     const float win_size_x, const float win_size_y,
			                     const float win_pos_x , const float win_pos_y);
		virtual void render_close_button(ImGuiWrapper& imgui,
			                             const float win_size_x, const float win_size_y,
			                             const float win_pos_x , const float win_pos_y);
		virtual void render_hypertext(ImGuiWrapper& imgui,
			                          const float text_x, const float text_y,
		                              const std::string text,
		                              bool more = false);
		// Left sign could be error or warning sign
		virtual void render_left_sign(ImGuiWrapper& imgui);
		virtual void render_minimize_button(ImGuiWrapper& imgui,
			                                const float win_pos_x, const float win_pos_y);
		// Hypertext action, returns true if notification should close.
		// Action is stored in NotificationData::callback as std::function<bool(wxEvtHandler*)>
		virtual bool on_text_click();
	
		// Part of init(), counts horizontal spacing like left indentation 
		virtual void count_spaces();
		// Part of init(), counts end lines
		virtual void count_lines();
		// returns true if PopStyleColor should be called later to pop this push
		virtual bool push_background_color();
		// used this function instead of reading directly m_data.duration. Some notifications might need to return changing value.
		virtual int  get_duration() { return m_data.duration; }

		const NotificationData m_data;
		// For reusing ImGUI windows.
		NotificationIDProvider &m_id_provider;
		int              m_id{ 0 };

		// State for rendering
		EState           m_state                { EState::Unknown };

		// Time values for rendering fade-out

		int64_t		 	 m_fading_start{ 0LL };

		// first appereance of notification or last hover;
		int64_t		 	 m_notification_start;
		// time to next must-do render
		int64_t          m_next_render{ std::numeric_limits<int64_t>::max() };
		float            m_current_fade_opacity{ 1.0f };

		// Notification data

		// Main text
		std::string      m_text1;
		// Clickable text
		std::string      m_hypertext;
		// Aditional text after hypertext - currently not used
		std::string      m_text2;

		// inner variables to position notification window, texts and buttons correctly

		// all space without text
		float            m_window_width_offset;
		// Space on left side without text
		float            m_left_indentation;
		// Total size of notification window - varies based on monitor
		float            m_window_height        { 56.0f };  
		float            m_window_width         { 450.0f };
		//Distance from bottom of notifications to top of this notification
		float            m_top_y                { 0.0f };  
		// Height of text - Used as basic scaling unit!
		float            m_line_height;
		// endlines for text1, hypertext excluded
        std::vector<size_t> m_endlines;
		// endlines for text2
		std::vector<size_t> m_endlines2;
		// Gray are f.e. eorrors when its uknown if they are still valid
		bool             m_is_gray              { false };
		//if multiline = true, notification is showing all lines(>2)
		bool             m_multiline            { false };
		// True if minimized button is rendered, helps to decide where is area for invisible close button
		bool             m_minimize_b_visible   { false };
        size_t           m_lines_count{ 1 };
	    // Target for wxWidgets events sent by clicking on the hyperlink available at some notifications.
		wxEvtHandler*    m_evt_handler;
	};

	

	class ObjectIDNotification : public PopNotification
	{
	public:
		ObjectIDNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler) 
			: PopNotification(n, id_provider, evt_handler) 
		{}
		ObjectID 	object_id;
		int    		warning_step { 0 };
	};

	class PlaterWarningNotification : public PopNotification
	{
	public:
		PlaterWarningNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler) : PopNotification(n, id_provider, evt_handler) {}
		void	     close()  override { if(is_finished()) return; m_state = EState::Hidden; wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0); }
		void		 real_close()      { m_state = EState::ClosePending; wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0); }
		void         show()            { m_state = EState::Unknown; }
	};

	
	class ProgressBarNotification : public PopNotification
	{
	public:
		
		ProgressBarNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler) : PopNotification(n, id_provider, evt_handler) { }
		virtual void set_percentage(float percent) { m_percentage = percent; }
	protected:
		virtual void init() override;
		virtual void count_lines() override;
		
		virtual void	render_text(ImGuiWrapper& imgui,
									const float win_size_x, const float win_size_y,
									const float win_pos_x, const float win_pos_y) override;
		virtual void	render_bar(ImGuiWrapper& imgui,
									const float win_size_x, const float win_size_y,
									const float win_pos_x, const float win_pos_y) ;
		virtual void	render_cancel_button(ImGuiWrapper& imgui,
									const float win_size_x, const float win_size_y,
									const float win_pos_x, const float win_pos_y)
		{}
		void			render_minimize_button(ImGuiWrapper& imgui,
			const float win_pos_x, const float win_pos_y) override {}
		float				m_percentage {0.0f};
		
		bool				m_has_cancel_button {false};
		bool                m_render_percentage {false};
		// local time of last hover for showing tooltip
		
	};

	

	class PrintHostUploadNotification : public ProgressBarNotification
	{
	public:
		enum class UploadJobState
		{
			PB_PROGRESS,
			PB_ERROR,
			PB_CANCELLED,
			PB_COMPLETED
		};
		PrintHostUploadNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, float percentage, int job_id, float filesize)
			:ProgressBarNotification(n, id_provider, evt_handler)
			, m_job_id(job_id)
			, m_file_size(filesize)
		{
			m_has_cancel_button = true;
			set_percentage(percentage);
		}
		static std::string	get_upload_job_text(int id, const std::string& filename, const std::string& host) { return /*"[" + std::to_string(id) + "] " + */filename + " -> " + host; }
		void				set_percentage(float percent) override;
		void				cancel() { m_uj_state = UploadJobState::PB_CANCELLED; m_has_cancel_button = false; }
		void				error()  { m_uj_state = UploadJobState::PB_ERROR;     m_has_cancel_button = false; init(); }
		bool				compare_job_id(const int other_id) const { return m_job_id == other_id; }
		bool				compare_text(const std::string& text) const override { return false; }
	protected:
		void        init() override;
		void		count_spaces() override;
		bool		push_background_color() override;
		void		render_bar(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y) override;
		void		render_cancel_button(ImGuiWrapper& imgui,
											const float win_size_x, const float win_size_y,
											const float win_pos_x, const float win_pos_y) override;
		void		render_left_sign(ImGuiWrapper& imgui) override;
		// Identifies job in cancel callback
		int					m_job_id;
		// Size of uploaded size to be displayed in MB
		float			    m_file_size;
		long				m_hover_time{ 0 };
		UploadJobState		m_uj_state{ UploadJobState::PB_PROGRESS };
	};

	class SlicingProgressNotification : public ProgressBarNotification
	{
	public:
		// Inner state of notification, Each state changes bahaviour of the notification
		enum class SlicingProgressState
		{
			SP_NO_SLICING, // hidden
			SP_BEGAN, // still hidden but allows to go to SP_PROGRESS state. This prevents showing progress after slicing was canceled.
			SP_PROGRESS, // never fades outs, no close button, has cancel button
			SP_CANCELLED, // fades after 10 seconds, simple message
			SP_COMPLETED // Has export hyperlink and print info, fades after 20 sec if sidebar is shown, otherwise no fade out
		};
		SlicingProgressNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, std::function<bool()> callback)
		: ProgressBarNotification(n, id_provider, evt_handler)
		, m_cancel_callback(callback)
		{
			set_progress_state(SlicingProgressState::SP_NO_SLICING);
			m_has_cancel_button = false;
			m_render_percentage = true;
		}
		// sets text of notification - call after setting progress state
		void				set_status_text(const std::string& text);
		// sets cancel button callback
		void			    set_cancel_callback(std::function<bool()> callback) { m_cancel_callback = callback; }
		bool                has_cancel_callback() const { return m_cancel_callback != nullptr; }
		// sets SlicingProgressState, negative percent means canceled, returns true if state was set succesfully.
		bool				set_progress_state(float percent);
		// sets SlicingProgressState, percent is used only at progress state. Returns true if state was set succesfully.
		bool				set_progress_state(SlicingProgressState state,float percent = 0.f);
		// sets additional string of print info and puts notification into Completed state.
		void			    set_print_info(const std::string& info);
		// sets fading if in Completed state.
		void                set_sidebar_collapsed(bool collapsed);
		// Calls inherited update_state and ensures Estate goes to hidden not closing.
		bool                update_state(bool paused, const int64_t delta) override;
		// Switch between technology to provide correct text.
		void				set_fff(bool b) { m_is_fff = b; }
		void				set_fdm(bool b) { m_is_fff = b; }
		void				set_sla(bool b) { m_is_fff = !b; }
		void                set_export_possible(bool b) { m_export_possible = b; }
	protected:
		void        init() override;
		void        count_lines() override 
		{
			if (m_sp_state == SlicingProgressState::SP_PROGRESS)
				ProgressBarNotification::count_lines();
			else
				PopNotification::count_lines();
		}
		void	    render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y) override;
		void		render_bar(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y) override;
		void		render_cancel_button(ImGuiWrapper& imgui,
											const float win_size_x, const float win_size_y,
											const float win_pos_x, const float win_pos_y) override;
		void		render_close_button(ImGuiWrapper& imgui,
										const float win_size_x, const float win_size_y,
										const float win_pos_x, const float win_pos_y) override;
		void		render_hypertext(ImGuiWrapper& imgui,
										const float text_x, const float text_y,
										const std::string text,
										bool more = false) override ;
		void       on_cancel_button();
		int		   get_duration() override;
		// if returns false, process was already canceled
		std::function<bool()>	m_cancel_callback;
		SlicingProgressState	m_sp_state { SlicingProgressState::SP_PROGRESS };
		bool				    m_has_print_info { false };
		std::string             m_print_info;
		bool                    m_sidebar_collapsed { false };
		bool					m_is_fff { true };
		// if true, it is possible show export hyperlink in state SP_PROGRESS
		bool                    m_export_possible { false };
	};

	class ProgressIndicatorNotification : public ProgressBarNotification
	{
	public:
		enum class ProgressIndicatorState
		{
			PIS_HIDDEN, // hidden
			PIS_PROGRESS_REQUEST, // progress was updated, request render on next update_state() call
			PIS_PROGRESS_UPDATED, // render was requested
			PIS_COMPLETED // both completed and canceled state. fades out into PIS_NO_SLICING
		};
		ProgressIndicatorNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler) 
		: ProgressBarNotification(n, id_provider, evt_handler) 
		{
			m_render_percentage = true;
		}
		// ProgressIndicator 
		void set_range(int range) { m_range = range; }
		void set_cancel_callback(CancelFn callback) { m_cancel_callback = callback; }
		void set_progress(int pr) { set_percentage((float)pr / (float)m_range); }
		void set_status_text(const char*); // utf8 char array
		int  get_range() const { return m_range; }
		// ProgressBarNotification
		void init() override;
		void set_percentage(float percent) override;
		bool update_state(bool paused, const int64_t delta) override;
		// Own
	protected:
		int						m_range { 100 };
		CancelFn				m_cancel_callback { nullptr };
		ProgressIndicatorState  m_progress_state { ProgressIndicatorState::PIS_HIDDEN };

		void		render_close_button(ImGuiWrapper& imgui,
			                            const float win_size_x, const float win_size_y,
			                            const float win_pos_x, const float win_pos_y) override;
		void		render_cancel_button(ImGuiWrapper& imgui,
									     const float win_size_x, const float win_size_y,
									     const float win_pos_x, const float win_pos_y) override;
		void        on_cancel_button() { if (m_cancel_callback) m_cancel_callback(); }
	};

	class ExportFinishedNotification : public PopNotification
	{
	public:
		ExportFinishedNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, bool to_removable,const std::string& export_path,const std::string& export_dir_path)
			: PopNotification(n, id_provider, evt_handler)
			, m_to_removable(to_removable)
			, m_export_path(export_path)
			, m_export_dir_path(export_dir_path)
		    {
				m_multiline = true;
			}
		bool        m_to_removable;
		std::string m_export_path;
		std::string m_export_dir_path;
	protected:
		// Reserves space on right for more buttons
		void count_spaces() override;
		void render_text(ImGuiWrapper& imgui,
						 const float win_size_x, const float win_size_y,
						 const float win_pos_x, const float win_pos_y) override;
		// Renders also button to open directory with exported path and eject removable media
		void render_close_button(ImGuiWrapper& imgui,
								 const float win_size_x, const float win_size_y,
								 const float win_pos_x, const float win_pos_y) override;
		void render_eject_button(ImGuiWrapper& imgui,
			                             const float win_size_x, const float win_size_y,
			                             const float win_pos_x, const float win_pos_y);
		void render_minimize_button(ImGuiWrapper& imgui, const float win_pos_x, const float win_pos_y) override
			{ m_minimize_b_visible = false; }
		bool on_text_click() override;
		void on_eject_click();
		// local time of last hover for showing tooltip
		long      m_hover_time { 0 };
		bool	  m_eject_pending { false };
	};

	class UpdatedItemsInfoNotification : public PopNotification
	{
	public:
		UpdatedItemsInfoNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, InfoItemType info_item_type)
			: PopNotification(n, id_provider, evt_handler)
		{
			//m_types_and_counts.emplace_back(info_item_type, 1);
		}
		void count_spaces() override;
		void add_type(InfoItemType type);
		void close() override{ 
			for (auto& tac : m_types_and_counts)
				tac.second = 0;
			PopNotification::close(); 
		}
	protected:
		void render_left_sign(ImGuiWrapper& imgui) override;
		std::vector<std::pair<InfoItemType, size_t>> m_types_and_counts;
	};

	// in HintNotification.hpp
	class HintNotification;

	// Data of waiting notifications
	struct DelayedNotification
	{
		std::unique_ptr<PopNotification>	notification;
		std::function<bool(void)>           condition_callback;
		int64_t								remaining_time;
		int64_t                             delay_interval;
		
		DelayedNotification(std::unique_ptr<PopNotification> n, std::function<bool(void)> cb, int64_t r, int64_t d)
		: notification(std::move(n))
	    , condition_callback(cb)
		, remaining_time(r)
		, delay_interval(d)
		{}
	};

	//pushes notification into the queue of notifications that are rendered
	//can be used to create custom notification
	bool push_notification_data(const NotificationData& notification_data, int timestamp);
	bool push_notification_data(std::unique_ptr<NotificationManager::PopNotification> notification, int timestamp);
	// Delayed notifications goes first to the m_waiting_notifications vector and only after remaining time is <= 0
	// and condition callback is success, notification is regular pushed from update function.
	// Otherwise another delay interval waiting. Timestamp is 0. 
	// Note that notification object is constructed when being added to the waiting list, but there are no updates called on it and its timer is reset at regular push.
	// Also note that no control of same notification is done during push_delayed_notification_data but if waiting notif fails to push, it continues waiting.
	void push_delayed_notification_data(std::unique_ptr<NotificationManager::PopNotification> notification, std::function<bool(void)> condition_callback, int64_t initial_delay, int64_t delay_interval);
	//finds older notification of same type and moves it to the end of queue. returns true if found
	bool activate_existing(const NotificationManager::PopNotification* notification);
	// Put the more important notifications to the bottom of the list.
	void sort_notifications();
	// If there is some error notification active, then the "Export G-code" notification after the slicing is finished is suppressed.
    bool has_slicing_error_notification();
	size_t get_standard_duration(NotificationLevel level)
	{
		switch (level) {
		
		case NotificationLevel::ErrorNotificationLevel: 			return 0;
		case NotificationLevel::WarningNotificationLevel:			return 0;
		case NotificationLevel::ImportantNotificationLevel:			return 0;
		case NotificationLevel::ProgressBarNotificationLevel:		return 2;
		case NotificationLevel::PrintInfoShortNotificationLevel:	return 5;
		case NotificationLevel::RegularNotificationLevel: 			return 10;
		case NotificationLevel::PrintInfoNotificationLevel:			return 10;
		case NotificationLevel::HintNotificationLevel:				return 300;
		default: return 10;
		}
	}

	// set by init(), until false notifications are only added not updated and frame is not requested after push
	bool m_initialized{ false };
	// Target for wxWidgets events sent by clicking on the hyperlink available at some notifications.
	wxEvtHandler*                m_evt_handler;
	// Cache of IDs to identify and reuse ImGUI windows.
	NotificationIDProvider 		 m_id_provider;
	std::deque<std::unique_ptr<PopNotification>> m_pop_notifications;
	// delayed waiting notifications, first is remaining time
	std::vector<DelayedNotification> m_waiting_notifications;
	//timestamps used for slicing finished - notification could be gone so it needs to be stored here
	std::unordered_set<int>      m_used_timestamps;
	// True if G-code preview is active. False if the Plater is active.
	bool                         m_in_preview { false };
	// True if the layer editing is enabled in Plater, so that the notifications are shifted left of it.
	bool                         m_move_from_overlay { false };
	// Timestamp of last rendering
	int64_t						 m_last_render { 0LL };
	// Notification types that can be shown multiple types at once (compared by text)
	const std::vector<NotificationType> m_multiple_types = { 
		NotificationType::CustomNotification, 
		NotificationType::PlaterWarning, 
		NotificationType::ProgressBar, 
		NotificationType::PrintHostUpload, 
        NotificationType::SimplifySuggestion
	};
	//prepared (basic) notifications
	static const NotificationData basic_notifications[];
};

}//namespace GUI
}//namespace Slic3r

#endif //slic3r_GUI_NotificationManager_hpp_
