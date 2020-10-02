#ifndef slic3r_GUI_NotificationManager_hpp_
#define slic3r_GUI_NotificationManager_hpp_

#include "Event.hpp"
#include "I18N.hpp"

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
using PresetUpdateAviableClickedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_PRESET_UPDATE_AVIABLE_CLICKED, PresetUpdateAviableClickedEvent);

class GLCanvas3D;
class ImGuiWrapper;

enum class NotificationType
{
	CustomNotification,
	SlicingComplete,
	SlicingNotPossible,
	ExportToRemovableFinished,
	Mouse3dDisconnected,
	Mouse3dConnected,
	NewPresetsAviable,
	NewAppAviable,
	PresetUpdateAviable,
	LoadingFailed,
	ValidateError, // currently not used - instead Slicing error is used for both slicing and validate errors
	SlicingError,
	SlicingWarning,
	PlaterError,
	PlaterWarning,
	ApplyError

};
class NotificationManager
{
public:
	enum class NotificationLevel : int
	{
		ErrorNotification =     4,
		WarningNotification =   3,
		ImportantNotification = 2,
		RegularNotification   = 1,
	};
	// duration 0 means not disapearing
	struct NotificationData {
		NotificationType    type;
		NotificationLevel   level;
		const int           duration;
		const std::string   text1;
		const std::string   hypertext = std::string();
		const std::string   text2     = std::string();
	};

	//Pop notification - shows only once to user.
	class PopNotification
	{
	public:
		enum class RenderResult
		{
			Finished,
			ClosePending,
			Static,
			Countdown,
			Hovered
		};
		 PopNotification(const NotificationData &n, const int id, wxEvtHandler* evt_handler);
		virtual ~PopNotification();
		RenderResult           render(GLCanvas3D& canvas, const float& initial_y, bool move_from_overlay, float overlay_width, bool move_from_slope, float slope_width);
		// close will dissapear notification on next render
		void                   close() { m_close_pending = true; }
		// data from newer notification of same type
		void                   update(const NotificationData& n);
		bool                   get_finished() const { return m_finished; }
		// returns top after movement
		float                  get_top() const { return m_top_y; }
		//returns top in actual frame
		float                  get_current_top() const { return m_top_y; }
		const NotificationType get_type() const { return m_data.type; }
		const NotificationData get_data() const { return m_data;  }
		const bool             get_is_gray() const { return m_is_gray; }
		// Call equals one second down
		void                   substract_remaining_time() { m_remaining_time--; }
		void                   set_gray(bool g) { m_is_gray = g; }
		void                   set_paused(bool p) { m_paused = p; }
		bool                   compare_text(const std::string& text);
        void                   hide(bool h) { m_hidden = h; }
	protected:
		// Call after every size change
		void         init();
		// Calculetes correct size but not se it in imgui!
		virtual void set_next_window_size(ImGuiWrapper& imgui);
		virtual void render_text(ImGuiWrapper& imgui,
			                     const float win_size_x, const float win_size_y,
			                     const float win_pos_x , const float win_pos_y);
		void         render_close_button(ImGuiWrapper& imgui,
			                             const float win_size_x, const float win_size_y,
			                             const float win_pos_x , const float win_pos_y);
		void         render_countdown(ImGuiWrapper& imgui,
			                          const float win_size_x, const float win_size_y,
			                          const float win_pos_x , const float win_pos_y);
		void         render_hypertext(ImGuiWrapper& imgui,
			                          const float text_x, const float text_y,
		                              const std::string text,
		                              bool more = false);
		void         render_left_sign(ImGuiWrapper& imgui);
		void         render_minimize_button(ImGuiWrapper& imgui,
			                                const float win_pos_x, const float win_pos_y);
		void         on_text_click();

		const NotificationData m_data;

		int              m_id;
		bool			 m_initialized          { false };
		// Main text
		std::string      m_text1;
		// Clickable text
		std::string      m_hypertext;
		// Aditional text after hypertext - currently not used
		std::string      m_text2;
		// Countdown variables
		long             m_remaining_time;
		bool             m_counting_down;
		long             m_last_remaining_time;
		bool             m_paused               { false };
		int              m_countdown_frame      { 0 };
		bool             m_fading_out           { false };
		// total time left when fading beggins
		float            m_fading_time          { 0.0f }; 
		float            m_current_fade_opacity { 1.f };
		// If hidden the notif is alive but not visible to user
		bool             m_hidden               { false };
		//  m_finished = true - does not render, marked to delete
		bool             m_finished             { false }; 
		// Will go to m_finished next render
		bool             m_close_pending        { false }; 
		// variables to count positions correctly
		float            m_window_width_offset;
		float            m_left_indentation;
		// Total size of notification window - varies based on monitor
		float            m_window_height        { 56.0f };  
		float            m_window_width         { 450.0f };
		//Distance from bottom of notifications to top of this notification
		float            m_top_y                { 0.0f };  
		
		// Height of text
		// Used as basic scaling unit!
		float            m_line_height;
		std::vector<int> m_endlines;
		// Gray are f.e. eorrors when its uknown if they are still valid
		bool             m_is_gray              { false };
		//if multiline = true, notification is showing all lines(>2)
		bool             m_multiline            { false };
		int              m_lines_count{ 1 };
		wxEvtHandler*    m_evt_handler;
	};

	class SlicingCompleteLargeNotification : public PopNotification
	{
	public:
		SlicingCompleteLargeNotification(const NotificationData& n, const int id, wxEvtHandler* evt_handler, bool largeds);
		void set_large(bool l);
		bool get_large() { return m_is_large; }

		void set_print_info(std::string info);
	protected:
		virtual void render_text(ImGuiWrapper& imgui,
			                     const float win_size_x, const float win_size_y,
			                     const float win_pos_x, const float win_pos_y) 
			                     override;

		bool        m_is_large;
		bool        m_has_print_info { false };
		std::string m_print_info { std::string() };
	};

	class SlicingWarningNotification : public PopNotification
	{
	public:
		SlicingWarningNotification(const NotificationData& n, const int id, wxEvtHandler* evt_handler) : PopNotification(n, id, evt_handler) {}
		void         set_object_id(size_t id) { object_id = id; }
		const size_t get_object_id() { return object_id; }
		void         set_warning_step(int ws) { warning_step = ws; }
		const int    get_warning_step() { return warning_step; }
	protected:
		size_t object_id;
		int    warning_step;
	};

	NotificationManager(wxEvtHandler* evt_handler);
	~NotificationManager();

	
	// only type means one of basic_notification (see below)
	void push_notification(const NotificationType type, GLCanvas3D& canvas, int timestamp = 0);
	// only text means Undefined type
	void push_notification(const std::string& text, GLCanvas3D& canvas, int timestamp = 0);
	void push_notification(const std::string& text, NotificationLevel level, GLCanvas3D& canvas, int timestamp = 0);
	// creates Slicing Error notification with custom text
	void push_slicing_error_notification(const std::string& text, GLCanvas3D& canvas);
	// creates Slicing Warning notification with custom text
	void push_slicing_warning_notification(const std::string& text, bool gray, GLCanvas3D& canvas, size_t oid, int warning_step);
	// marks slicing errors as gray
	void set_all_slicing_errors_gray(bool g);
	// marks slicing warings as gray
	void set_all_slicing_warnings_gray(bool g);
	void set_slicing_warning_gray(const std::string& text, bool g);
	// imidietly stops showing slicing errors
	void close_slicing_errors_and_warnings();
	void compare_warning_oids(const std::vector<size_t>& living_oids);
	void push_plater_error_notification(const std::string& text, GLCanvas3D& canvas);
	void push_plater_warning_notification(const std::string& text, GLCanvas3D& canvas);
	// Closes error or warning of same text
	void close_plater_error_notification(const std::string& text);
	void close_plater_warning_notification(const std::string& text);
	// creates special notification slicing complete
	// if large = true prints printing time and export button 
	void push_slicing_complete_notification(GLCanvas3D& canvas, int timestamp, bool large);
	void set_slicing_complete_print_time(std::string info);
	void set_slicing_complete_large(bool large);
	// renders notifications in queue and deletes expired ones
	void render_notifications(GLCanvas3D& canvas, float overlay_width, float slope_width);
	// finds and closes all notifications of given type
	void close_notification_of_type(const NotificationType type);
	void dpi_changed();
    void set_in_preview(bool preview);
	// Move to left to avoid colision with variable layer height gizmo
	void set_move_from_overlay(bool move) { m_move_from_overlay = move; }
	// or slope visualization gizmo
	void set_move_from_slope (bool move) { m_move_from_slope = move; }
private:
	//pushes notification into the queue of notifications that are rendered
	//can be used to create custom notification
	bool push_notification_data(const NotificationData& notification_data, GLCanvas3D& canvas, int timestamp);
	bool push_notification_data(NotificationManager::PopNotification* notification, GLCanvas3D& canvas, int timestamp);
	//finds older notification of same type and moves it to the end of queue. returns true if found
	bool find_older(NotificationManager::PopNotification* notification);
	void sort_notifications();
    bool has_error_notification();

	wxEvtHandler*                m_evt_handler;
	std::deque<PopNotification*> m_pop_notifications;
	int                          m_next_id { 1 };
	long                         m_last_time { 0 };
	bool                         m_hovered { false };
	//timestamps used for slining finished - notification could be gone so it needs to be stored here
	std::unordered_set<int>      m_used_timestamps;
	bool                         m_in_preview { false };
	bool                         m_move_from_overlay { false };
	bool                         m_move_from_slope{ false };

	//prepared (basic) notifications
	const std::vector<NotificationData> basic_notifications = {
		{NotificationType::SlicingNotPossible, NotificationLevel::RegularNotification, 10,  _u8L("Slicing is not possible.")},
		{NotificationType::ExportToRemovableFinished, NotificationLevel::ImportantNotification, 0,  _u8L("Exporting finished."),  _u8L("Eject drive.") },
		{NotificationType::Mouse3dDisconnected, NotificationLevel::RegularNotification, 10,  _u8L("3D Mouse disconnected.") },
		{NotificationType::Mouse3dConnected, NotificationLevel::RegularNotification, 5,  _u8L("3D Mouse connected.") },
		{NotificationType::NewPresetsAviable, NotificationLevel::ImportantNotification, 20,  _u8L("New Presets are available."),  _u8L("See here.") },
		{NotificationType::PresetUpdateAviable, NotificationLevel::ImportantNotification, 20,  _u8L("Configuration update is available."),  _u8L("See more.")},
		{NotificationType::NewAppAviable, NotificationLevel::ImportantNotification, 20,  _u8L("New version is available."),  _u8L("See Releases page.")},
		//{NotificationType::NewAppAviable, NotificationLevel::ImportantNotification, 20,  _u8L("New vesion of PrusaSlicer is available.",  _u8L("Download page.") },
		//{NotificationType::LoadingFailed, NotificationLevel::RegularNotification, 20,  _u8L("Loading of model has Failed") },
		//{NotificationType::DeviceEjected, NotificationLevel::RegularNotification, 10,  _u8L("Removable device has been safely ejected")} // if we want changeble text (like here name of device), we need to do it as CustomNotification
	};
};

}//namespace GUI
}//namespace Slic3r

#endif //slic3r_GUI_NotificationManager_hpp_