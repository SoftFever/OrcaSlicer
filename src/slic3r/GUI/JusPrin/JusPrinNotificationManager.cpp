#include "JusPrinNotificationManager.hpp"
#include "JusPrinChatPanel.hpp"
#include "slic3r/GUI/SlicingProgressNotification.hpp"

namespace Slic3r {
namespace GUI {

void JusPrinNotificationManager::render_notifications(GLCanvas3D& canvas, float overlay_width, float bottom_margin, float right_margin)
{
    // Check for SlicingProgressNotification instances and print their percentage
    for (const auto& notification : get_pop_notifications()) {
        const auto* slicing_progress = dynamic_cast<const NotificationManager::SlicingProgressNotification*>(notification.get());
        if (!slicing_progress) continue;
        float percentage = slicing_progress->get_percentage();
        if (percentage <= 0) continue;
        wxGetApp().plater()->jusprinChatPanel()->SendSlicingProgressEvent(percentage, slicing_progress->get_text1());
        break; // Only need to find the first one
    }

    // Call parent implementation for normal rendering
    NotificationManager::render_notifications(canvas, overlay_width, bottom_margin, right_margin);
}

void JusPrinNotificationManager::push_notification(const NotificationType type, int timestamp)
{
    push_notification_to_chat("Something happened", JusPrinNotificationManager::get_notification_type_name(type), JusPrinNotificationManager::get_notification_level_name(NotificationLevel::RegularNotificationLevel));
}

void JusPrinNotificationManager::push_notification(const std::string& text, int timestamp)
{
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(NotificationType::CustomNotification), JusPrinNotificationManager::get_notification_level_name(NotificationLevel::RegularNotificationLevel));
}

void JusPrinNotificationManager::push_notification(NotificationType type, NotificationLevel level, const std::string& text,
                                                 const std::string& hypertext,
                                                 std::function<bool(wxEvtHandler*)> callback,
                                                 int timestamp)
{
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(type), JusPrinNotificationManager::get_notification_level_name(level));
}

void JusPrinNotificationManager::push_validate_error_notification(StringObjectException const& error)
{
    push_notification_to_chat(
        error.string,
        get_notification_type_name(NotificationType::ValidateError),
        get_notification_level_name(NotificationLevel::ErrorNotificationLevel)
    );
}

void JusPrinNotificationManager::push_upload_job_notification(int id, float filesize, const std::string& filename,
                                                            const std::string& host, float percentage)
{
    std::string text = PrintHostUploadNotification::get_upload_job_text(id, filename, host);
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(NotificationType::PrintHostUpload));
}

void JusPrinNotificationManager::push_slicing_error_notification(const std::string& text, std::vector<ModelObject const*> objs)
{
   push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(NotificationType::SlicingError), JusPrinNotificationManager::get_notification_level_name(NotificationLevel::ErrorNotificationLevel));
}

void JusPrinNotificationManager::push_slicing_warning_notification(const std::string& text, bool gray, ModelObject const* obj,
                                                                 ObjectID oid, int warning_step, int warning_msg_id,
                                                                 NotificationLevel level)
{
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(NotificationType::SlicingWarning), JusPrinNotificationManager::get_notification_level_name(NotificationLevel::WarningNotificationLevel));
}

void JusPrinNotificationManager::push_plater_error_notification(const std::string& text)
{
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(NotificationType::PlaterError), JusPrinNotificationManager::get_notification_level_name(NotificationLevel::ErrorNotificationLevel));
}

void JusPrinNotificationManager::push_plater_warning_notification(const std::string& text)
{
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(NotificationType::PlaterWarning), JusPrinNotificationManager::get_notification_level_name(NotificationLevel::WarningNotificationLevel));
}

void JusPrinNotificationManager::push_simplify_suggestion_notification(const std::string& text, ObjectID object_id,
                                                                     const std::string& hypertext,
                                                                     std::function<bool(wxEvtHandler*)> callback)
{
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(NotificationType::SimplifySuggestion), JusPrinNotificationManager::get_notification_level_name(NotificationLevel::PrintInfoNotificationLevel));
}

void JusPrinNotificationManager::push_exporting_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable)
{
    std::string text = _u8L("Export successfully.") + "\n" + path;
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(NotificationType::ExportFinished), JusPrinNotificationManager::get_notification_level_name(NotificationLevel::RegularNotificationLevel));
}

void JusPrinNotificationManager::push_import_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable)
{
    std::string text = _u8L("Model file downloaded.") + "\n" + path;
    push_notification_to_chat(text, JusPrinNotificationManager::get_notification_type_name(NotificationType::ExportFinished), JusPrinNotificationManager::get_notification_level_name(NotificationLevel::RegularNotificationLevel));
}

void JusPrinNotificationManager::push_notification_to_chat(const std::string& text, const std::string& type, const std::string& level) {
    if (wxGetApp().plater() && wxGetApp().plater()->jusprinChatPanel()) {
        wxGetApp().plater()->jusprinChatPanel()->SendNotificationPushedEvent(text, type, level);
    }
}

void JusPrinNotificationManager::bbl_show_objectsinfo_notification(const std::string& text, bool is_warning, bool is_hidden) {
    std::string level = is_warning ? "warning" : "regular";
    push_notification_to_chat(text, "BBLObjectInfo", level);
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

}//namespace GUI
}//namespace Slic3r
