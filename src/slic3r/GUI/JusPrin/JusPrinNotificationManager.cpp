#include "JusPrinNotificationManager.hpp"
#include "JusPrinChatPanel.hpp"

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
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(type), JusPrinNotificationManager::get_notification_level_name(level));
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

std::string JusPrinNotificationManager::get_notification_type_name(NotificationType type) {
    switch (type) {
        case NotificationType::CustomNotification: return "CustomNotification";
        case NotificationType::ExportFinished: return "ExportFinished";
        case NotificationType::Mouse3dDisconnected: return "Mouse3dDisconnected";
        case NotificationType::NewAppAvailable: return "NewAppAvailable";
        case NotificationType::NewAlphaAvailable: return "NewAlphaAvailable";
        case NotificationType::NewBetaAvailable: return "NewBetaAvailable";
        case NotificationType::PresetUpdateAvailable: return "PresetUpdateAvailable";
        case NotificationType::PresetUpdateFinished: return "PresetUpdateFinished";
        case NotificationType::ValidateError: return "ValidateError";
        case NotificationType::ValidateWarning: return "ValidateWarning";
        case NotificationType::SlicingError: return "SlicingError";
        case NotificationType::SlicingSeriousWarning: return "SlicingSeriousWarning";
        case NotificationType::SlicingWarning: return "SlicingWarning";
        case NotificationType::PlaterError: return "PlaterError";
        case NotificationType::PlaterWarning: return "PlaterWarning";
        case NotificationType::ProgressBar: return "ProgressBar";
        default: return "UnknownNotificationType";
    }
}

std::string JusPrinNotificationManager::get_notification_level_name(NotificationLevel level) {
    switch (level) {
        case NotificationLevel::RegularNotificationLevel: return "regular";
        case NotificationLevel::ImportantNotificationLevel: return "important";
        case NotificationLevel::ErrorNotificationLevel: return "error";
        case NotificationLevel::WarningNotificationLevel: return "warning";
        case NotificationLevel::HintNotificationLevel: return "hint";
        case NotificationLevel::ProgressBarNotificationLevel: return "progress";
        default: return "unknown";
    }
}

void JusPrinNotificationManager::push_notification_to_chat(const std::string& text, const std::string& type, const std::string& level) {
    if (wxGetApp().plater() && wxGetApp().plater()->jusprinChatPanel()) {
        wxGetApp().plater()->jusprinChatPanel()->SendNotificationPushedEvent(text, type, level);
    }
}

}//namespace GUI
}//namespace Slic3r
