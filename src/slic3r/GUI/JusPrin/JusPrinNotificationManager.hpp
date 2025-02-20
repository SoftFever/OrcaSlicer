#ifndef slic3r_GUI_JusPrinNotificationManager_hpp_
#define slic3r_GUI_JusPrinNotificationManager_hpp_

#include "../NotificationManager.hpp"

namespace Slic3r {
namespace GUI {

class JusPrinNotificationManager : public NotificationManager
{
public:
    JusPrinNotificationManager(wxEvtHandler* evt_handler) : NotificationManager(evt_handler) {}

    // Core methods
    void render_notifications(GLCanvas3D& canvas, float overlay_width, float bottom_margin, float right_margin) override;
    bool update_notifications(GLCanvas3D& canvas) override;

    // Push methods
    void push_notification(const NotificationType type, int timestamp = 0) override;
    void push_notification(const std::string& text, int timestamp = 0) override;
    void push_notification(NotificationType type, NotificationLevel level, const std::string& text,
                         const std::string& hypertext = "",
                         std::function<bool(wxEvtHandler*)> callback = std::function<bool(wxEvtHandler*)>(),
                         int timestamp = 0) override;

    void push_validate_error_notification(StringObjectException const& error) override;
    void push_upload_job_notification(int id, float filesize, const std::string& filename, const std::string& host, float percentage = 0) override;
    void push_slicing_error_notification(const std::string& text, std::vector<ModelObject const*> objs) override;
    void push_slicing_warning_notification(const std::string& text, bool gray, ModelObject const* obj, ObjectID oid,
                                         int warning_step, int warning_msg_id,
                                         NotificationLevel level = NotificationLevel::WarningNotificationLevel) override;
    void push_plater_error_notification(const std::string& text) override;
    void push_plater_warning_notification(const std::string& text) override;
    void push_simplify_suggestion_notification(const std::string& text, ObjectID object_id,
                                             const std::string& hypertext = "",
                                             std::function<bool(wxEvtHandler*)> callback = std::function<bool(wxEvtHandler*)>()) override;
    void push_exporting_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable) override;
    void push_import_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable) override;

    // Override BBL notification methods
    void bbl_show_objectsinfo_notification(const std::string& text, bool is_warning, bool is_hidden) override;

    static std::string get_notification_type_name(NotificationType type);
    static std::string get_notification_level_name(NotificationLevel level);

private:
    void push_notification_to_chat(const std::string& text,
                                 const std::string& type = "",
                                 const std::string& level = "");

};

}//namespace GUI
}//namespace Slic3r

#endif //slic3r_GUI_JusPrinNotificationManager_hpp_
