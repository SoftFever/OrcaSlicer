#include "JusPrinNotificationManager.hpp"

namespace Slic3r {
namespace GUI {

void JusPrinNotificationManager::render_notifications(GLCanvas3D& canvas, float overlay_width, float bottom_margin, float right_margin)
{
    // Empty implementation for now
}

bool JusPrinNotificationManager::update_notifications(GLCanvas3D& canvas)
{
    // Empty implementation for now
    return false;
}

void JusPrinNotificationManager::push_notification(const NotificationType type, int timestamp)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_notification(const std::string& text, int timestamp)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_notification(NotificationType type, NotificationLevel level, const std::string& text,
                                                 const std::string& hypertext,
                                                 std::function<bool(wxEvtHandler*)> callback,
                                                 int timestamp)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_validate_error_notification(StringObjectException const& error)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_upload_job_notification(int id, float filesize, const std::string& filename,
                                                            const std::string& host, float percentage)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_slicing_error_notification(const std::string& text, std::vector<ModelObject const*> objs)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_slicing_warning_notification(const std::string& text, bool gray, ModelObject const* obj,
                                                                 ObjectID oid, int warning_step, int warning_msg_id,
                                                                 NotificationLevel level)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_plater_error_notification(const std::string& text)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_plater_warning_notification(const std::string& text)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_simplify_suggestion_notification(const std::string& text, ObjectID object_id,
                                                                     const std::string& hypertext,
                                                                     std::function<bool(wxEvtHandler*)> callback)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_exporting_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_import_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable)
{
    // Empty implementation for now
}

void JusPrinNotificationManager::push_delayed_notification(NotificationType type, std::function<bool(void)> condition_callback,
                                                         int64_t initial_delay, int64_t delay_interval)
{
    // Empty implementation for now
}

}//namespace GUI
}//namespace Slic3r
