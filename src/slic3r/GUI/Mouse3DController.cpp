#include "libslic3r/libslic3r.h"
#include "Mouse3DController.hpp"

#include "Camera.hpp"
#include "GUI_App.hpp"
#include "PresetBundle.hpp"
#include "AppConfig.hpp"
#if ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
#include "GLCanvas3D.hpp"
#endif // ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG

#include <wx/glcanvas.h>

#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>
#include "I18N.hpp"

#include <bitset>

// WARN: If updating these lists, please also update resources/udev/90-3dconnexion.rules

static const std::vector<int> _3DCONNEXION_VENDORS =
{
    0x046d,  // LOGITECH = 1133 // Logitech (3Dconnexion is made by Logitech)
    0x256F   // 3DCONNECTION = 9583 // 3Dconnexion
};

// See: https://github.com/FreeSpacenav/spacenavd/blob/a9eccf34e7cac969ee399f625aef827f4f4aaec6/src/dev.c#L202
static const std::vector<int> _3DCONNEXION_DEVICES =
{
    0xc603,	/* 50691 spacemouse plus XT */
    0xc605,	/* 50693 cadman */
    0xc606,	/* 50694 spacemouse classic */
    0xc621,	/* 50721 spaceball 5000 */
    0xc623,	/* 50723 space traveller */
    0xc625,	/* 50725 space pilot */
    0xc626,	/* 50726 space navigator *TESTED* */
    0xc627,	/* 50727 space explorer */
    0xc628,	/* 50728 space navigator for notebooks*/
    0xc629,	/* 50729 space pilot pro*/
    0xc62b,	/* 50731 space mouse pro*/
    0xc62e,	/* 50734 spacemouse wireless (USB cable) *TESTED* */
    0xc62f,	/* 50735 spacemouse wireless receiver */
    0xc631,	/* 50737 spacemouse pro wireless *TESTED* */
    0xc632,	/* 50738 spacemouse pro wireless receiver */
    0xc633,	/* 50739 spacemouse enterprise */
    0xc635,	/* 50741 spacemouse compact *TESTED* */
    0xc636,	/* 50742 spacemouse module */
    0xc640,	/* 50752 nulooq */
    0xc652, /* 50770 3Dconnexion universal receiver *TESTED* */
};

namespace Slic3r {
namespace GUI {
    
const double Mouse3DController::State::DefaultTranslationScale = 2.5;
const double Mouse3DController::State::MaxTranslationDeadzone = 0.2;
const double Mouse3DController::State::DefaultTranslationDeadzone = 0.5 * Mouse3DController::State::MaxTranslationDeadzone;
const float Mouse3DController::State::DefaultRotationScale = 1.0f;
const float Mouse3DController::State::MaxRotationDeadzone = (float)Mouse3DController::State::MaxTranslationDeadzone;
const float Mouse3DController::State::DefaultRotationDeadzone = 0.5f * Mouse3DController::State::MaxRotationDeadzone;

Mouse3DController::State::State()
    : m_buttons_enabled(false)
    , m_translation_params(DefaultTranslationScale, DefaultTranslationDeadzone)
    , m_rotation_params(DefaultRotationScale, DefaultRotationDeadzone)
    , m_mouse_wheel_counter(0)
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    , m_translation_queue_max_size(0)
    , m_rotation_queue_max_size(0)
    , m_buttons_queue_max_size(0)
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
{
}

void Mouse3DController::State::append_translation(const Vec3d& translation)
{
    while (m_translation.queue.size() >= m_translation.max_size)
    {
        m_translation.queue.pop();
    }
    m_translation.queue.push(translation);
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    m_translation_queue_max_size = std::max(m_translation_queue_max_size, m_translation.queue.size());
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
}

void Mouse3DController::State::append_rotation(const Vec3f& rotation)
{
    while (m_rotation.queue.size() >= m_rotation.max_size)
    {
        m_rotation.queue.pop();
    }
    m_rotation.queue.push(rotation);
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    m_rotation_queue_max_size = std::max(m_rotation_queue_max_size, m_rotation.queue.size());
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    if (rotation(0) != 0.0f)
        ++m_mouse_wheel_counter;
}

void Mouse3DController::State::append_button(unsigned int id)
{
    if (!m_buttons_enabled)
        return;

    m_buttons.push(id);
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    m_buttons_queue_max_size = std::max(m_buttons_queue_max_size, m_buttons.size());
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
}

bool Mouse3DController::State::process_mouse_wheel()
{
    if (m_mouse_wheel_counter == 0)
        return false;
    else if (!m_rotation.queue.empty())
    {
        --m_mouse_wheel_counter;
        return true;
    }

    m_mouse_wheel_counter = 0;
    return true;
}

void Mouse3DController::State::set_queues_max_size(size_t size)
{
    if (size > 0)
    {
        m_translation.max_size = size;
        m_rotation.max_size = size;

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        m_translation_queue_max_size = 0;
        m_rotation_queue_max_size = 0;
        m_buttons_queue_max_size = 0;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    }
}

bool Mouse3DController::State::apply(Camera& camera)
{
    if (!wxGetApp().IsActive())
        return false;

    bool ret = false;

    if (has_translation())
    {
        const Vec3d& translation = m_translation.queue.front();
        camera.set_target(camera.get_target() + m_translation_params.scale * (translation(0) * camera.get_dir_right() + translation(1) * camera.get_dir_forward() + translation(2) * camera.get_dir_up()));
        m_translation.queue.pop();
        ret = true;
    }

    if (has_rotation())
    {
        const Vec3f& rotation = m_rotation.queue.front();
        float theta = m_rotation_params.scale * rotation(0);
        float phi = m_rotation_params.scale * rotation(2);
        float sign = camera.inverted_phi ? -1.0f : 1.0f;
        camera.phi += sign * phi;
        camera.set_theta(camera.get_theta() + theta, wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA);
        m_rotation.queue.pop();
        ret = true;
    }

    if (m_buttons_enabled && has_button())
    {
        unsigned int button = m_buttons.front();
        switch (button)
        {
        case 0: { camera.update_zoom(1.0); break; }
        case 1: { camera.update_zoom(-1.0); break; }
        default: { break; }
        }
        m_buttons.pop();
        ret = true;
    }

    return ret;
}

Mouse3DController::Mouse3DController()
    : m_initialized(false)
    , m_device(nullptr)
    , m_device_str("")
    , m_running(false)
    , m_show_settings_dialog(false)
#if ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
    , m_settings_dialog_closed_by_user(false)
#endif // ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
{
    m_last_time = std::chrono::high_resolution_clock::now();
}

void Mouse3DController::init()
{
    if (m_initialized)
        return;

    // Initialize the hidapi library
    int res = hid_init();
    if (res != 0)
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to initialize hidapi library";
        return;
    }

    m_initialized = true;
}

void Mouse3DController::shutdown()
{
    if (!m_initialized)
        return;

    stop();
    disconnect_device();

    // Finalize the hidapi library
    hid_exit();
    m_initialized = false;
}

bool Mouse3DController::apply(Camera& camera)
{
    if (!m_initialized)
        return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    // check if the user unplugged the device
    if (!m_running && is_device_connected())
    {
        disconnect_device();
        // hides the settings dialog if the user un-plug the device
        m_show_settings_dialog = false;
#if ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
        m_settings_dialog_closed_by_user = false;
#endif // ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
    }

    // check if the user plugged the device
    if (connect_device())
        start();

    return is_device_connected() ? m_state.apply(camera) : false;
}

#if ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
void Mouse3DController::render_settings_dialog(GLCanvas3D& canvas) const
#else
void Mouse3DController::render_settings_dialog(unsigned int canvas_width, unsigned int canvas_height) const
#endif // ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
{
    if (!m_running || !m_show_settings_dialog)
        return;

#if ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
    // when the user clicks on [X] or [Close] button we need to trigger
    // an extra frame to let the dialog disappear
    if (m_settings_dialog_closed_by_user)
    {
        m_show_settings_dialog = false;
        m_settings_dialog_closed_by_user = false;
        canvas.request_extra_frame();
        return;
    }

    Size cnv_size = canvas.get_canvas_size();
#endif // ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG

    ImGuiWrapper& imgui = *wxGetApp().imgui();
#if ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
    imgui.set_next_window_pos(0.5f * (float)cnv_size.get_width(), 0.5f * (float)cnv_size.get_height(), ImGuiCond_Always, 0.5f, 0.5f);
#else
    imgui.set_next_window_pos(0.5f * (float)canvas_width, 0.5f * (float)canvas_height, ImGuiCond_Always, 0.5f, 0.5f);
#endif // ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG

#if ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG
    static ImVec2 last_win_size(0.0f, 0.0f);
    bool shown = true;
    if (imgui.begin(_(L("3Dconnexion settings")), &shown, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
    {
        if (shown)
        {
            ImVec2 win_size = ImGui::GetWindowSize();
            if ((last_win_size.x != win_size.x) || (last_win_size.y != win_size.y))
            {
                // when the user clicks on [X] button, the next time the dialog is shown 
                // has a dummy size, so we trigger an extra frame to let it have the correct size
                last_win_size = win_size;
                canvas.request_extra_frame();
            }
#else
    imgui.begin(_(L("3Dconnexion settings")), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
#endif // ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG

            const ImVec4& color = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text(_(L("Device:")));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            imgui.text(m_device_str);

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text(_(L("Speed:")));
            ImGui::PopStyleColor();

            float translation_scale = (float)m_state.get_translation_scale() / State::DefaultTranslationScale;
            if (imgui.slider_float(_(L("Translation")) + "##1", &translation_scale, 0.5f, 5.0f, "%.1f"))
                m_state.set_translation_scale(State::DefaultTranslationScale * (double)translation_scale);

            float rotation_scale = m_state.get_rotation_scale() / State::DefaultRotationScale;
            if (imgui.slider_float(_(L("Rotation")) + "##1", &rotation_scale, 0.5f, 5.0f, "%.1f"))
                m_state.set_rotation_scale(State::DefaultRotationScale * rotation_scale);

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text(_(L("Deadzone:")));
            ImGui::PopStyleColor();

            float translation_deadzone = (float)m_state.get_translation_deadzone();
            if (imgui.slider_float(_(L("Translation")) + "##2", &translation_deadzone, 0.0f, (float)State::MaxTranslationDeadzone, "%.2f"))
                m_state.set_translation_deadzone((double)translation_deadzone);

            float rotation_deadzone = m_state.get_rotation_deadzone();
            if (imgui.slider_float(_(L("Rotation")) + "##2", &rotation_deadzone, 0.0f, State::MaxRotationDeadzone, "%.2f"))
                m_state.set_rotation_deadzone(rotation_deadzone);

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            ImGui::Separator();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text("DEBUG:");
            imgui.text("Vectors:");
            ImGui::PopStyleColor();
            Vec3f translation = m_state.get_translation().cast<float>();
            Vec3f rotation = m_state.get_rotation();
            ImGui::InputFloat3("Translation##3", translation.data(), "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat3("Rotation##3", rotation.data(), "%.3f", ImGuiInputTextFlags_ReadOnly);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text("Queue size:");
            ImGui::PopStyleColor();

            int translation_size[2] = { (int)m_state.get_translation_queue_size(), (int)m_state.get_translation_queue_max_size() };
            int rotation_size[2] = { (int)m_state.get_rotation_queue_size(), (int)m_state.get_rotation_queue_max_size() };
            int buttons_size[2] = { (int)m_state.get_buttons_queue_size(), (int)m_state.get_buttons_queue_max_size() };

            ImGui::InputInt2("Translation##4", translation_size, ImGuiInputTextFlags_ReadOnly);
            ImGui::InputInt2("Rotation##4", rotation_size, ImGuiInputTextFlags_ReadOnly);
            ImGui::InputInt2("Buttons", buttons_size, ImGuiInputTextFlags_ReadOnly);

            int queue_size = (int)m_state.get_queues_max_size();
            if (ImGui::InputInt("Max size", &queue_size, 1, 1, ImGuiInputTextFlags_ReadOnly))
            {
                if (queue_size > 0)
                    m_state.set_queues_max_size(queue_size);
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text("Camera:");
            ImGui::PopStyleColor();
            Vec3f target = wxGetApp().plater()->get_camera().get_target().cast<float>();
            ImGui::InputFloat3("Target", target.data(), "%.3f", ImGuiInputTextFlags_ReadOnly);
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
#if ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG

            ImGui::Separator();
            if (imgui.button(_(L("Close"))))
            {
                // the user clicked on the [Close] button
                m_settings_dialog_closed_by_user = true;
                canvas.set_as_dirty();
            }
        }
        else
        {
            // the user clicked on the [X] button
            m_settings_dialog_closed_by_user = true;
            canvas.set_as_dirty();
        }
    }
#endif // ENABLE_3DCONNEXION_DEVICES_CLOSE_SETTING_DIALOG

    imgui.end();
}

bool Mouse3DController::connect_device()
{
    static const long long DETECTION_TIME_MS = 2000; // seconds

    if (is_device_connected())
        return false;

    // check time since last detection took place
    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - m_last_time).count() < DETECTION_TIME_MS)
        return false;

    m_last_time = std::chrono::high_resolution_clock::now();

    // Enumerates devices
    hid_device_info* devices = hid_enumerate(0, 0);
    if (devices == nullptr)
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to enumerate HID devices";
        return false;
    }

    // Searches for 1st connected 3Dconnexion device
    struct DeviceData
    {
        std::string path;
        unsigned short usage_page;
        unsigned short usage;

        DeviceData()
            : path(""), usage_page(0), usage(0)
        {}
        DeviceData(const std::string& path, unsigned short usage_page, unsigned short usage)
            : path(path), usage_page(usage_page), usage(usage)
        {}

        bool has_valid_usage() const { return (usage_page == 1) && (usage == 8); }
    };

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    hid_device_info* cur = devices;
    std::cout << std::endl << "======================================================================================================================================" << std::endl;
    std::cout << "Detected devices:" << std::endl;
    while (cur != nullptr)
    {
        std::cout << "\"";
        std::wcout << ((cur->manufacturer_string != nullptr) ? cur->manufacturer_string : L"Unknown");
        std::cout << "/";
        std::wcout << ((cur->product_string != nullptr) ? cur->product_string : L"Unknown");
        std::cout << "\" code: " << cur->vendor_id << "/" << cur->product_id << " (" << std::hex << cur->vendor_id << "/" << cur->product_id << std::dec << ")";
        std::cout << " serial number: '";
        std::wcout << ((cur->serial_number != nullptr) ? cur->serial_number : L"Unknown");
        std::cout << "' usage page: " << cur->usage_page << " usage: " << cur->usage << " interface number: " << cur->interface_number << std::endl;

        cur = cur->next;
    }
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

    // When using 3Dconnexion universal receiver, multiple devices are detected sharing the same vendor_id and product_id.
    // To choose from them the right one we use:
    // On Windows and Mac: usage_page == 1 and usage == 8
    // On Linux: as usage_page and usage are not defined (see hidapi.h) we try all detected devices until one is succesfully open
    // When only a single device is detected, as for wired connections, vendor_id and product_id are enough

    // First we count all the valid devices from the enumerated list,

    hid_device_info* current = devices;
    typedef std::pair<unsigned short, unsigned short> DeviceIds;
    typedef std::vector<DeviceData> DeviceDataList;
    typedef std::map<DeviceIds, DeviceDataList> DetectedDevices;
    DetectedDevices detected_devices;
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    std::cout << std::endl << "Detected 3D connexion devices:" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    while (current != nullptr)
    {
        unsigned short vendor_id = 0;
        unsigned short product_id = 0;

        for (size_t i = 0; i < _3DCONNEXION_VENDORS.size(); ++i)
        {
            if (_3DCONNEXION_VENDORS[i] == current->vendor_id)
            {
                vendor_id = current->vendor_id;
                break;
            }
        }

        if (vendor_id != 0)
        {
            for (size_t i = 0; i < _3DCONNEXION_DEVICES.size(); ++i)
            {
                if (_3DCONNEXION_DEVICES[i] == current->product_id)
                {
                    product_id = current->product_id;
                    DeviceIds detected_device(vendor_id, product_id);
                    DetectedDevices::iterator it = detected_devices.find(detected_device);
                    if (it == detected_devices.end())
                        it = detected_devices.insert(DetectedDevices::value_type(detected_device, DeviceDataList())).first;

                    it->second.emplace_back(current->path, current->usage_page, current->usage);

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    std::wcout << "\"" << ((current->manufacturer_string != nullptr) ? current->manufacturer_string : L"Unknown");
                    std::cout << "/";
                    std::wcout << ((current->product_string != nullptr) ? current->product_string : L"Unknown");
                    std::cout << "\" code: " << current->vendor_id << "/" << current->product_id << " (" << std::hex << current->vendor_id << "/" << current->product_id << std::dec << ")";
                    std::cout << " serial number: '";
                    std::wcout << ((current->serial_number != nullptr) ? current->serial_number : L"Unknown");
                    std::cout << "' usage page: " << current->usage_page << " usage: " << current->usage << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                }
            }
        }

        current = current->next;
    }

    // Free enumerated devices
    hid_free_enumeration(devices);

    if (detected_devices.empty())
        return false;

    std::string path = "";
    unsigned short vendor_id = 0;
    unsigned short product_id = 0;

    // Then we'll decide the choosing logic to apply in dependence of the device count and operating system

    for (const DetectedDevices::value_type& device : detected_devices)
    {
        if (device.second.size() == 1)
        {
#ifdef __linux__
            hid_device* test_device = hid_open(device.first.first, device.first.second, nullptr);
            if (test_device != nullptr)
            {
                hid_close(test_device);
#else
            if (device.second.front().has_valid_usage())
            {
#endif // __linux__
                vendor_id = device.first.first;
                product_id = device.first.second;
                break;
            }
        }
        else
        {
            bool found = false;
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            for (const DeviceData& data : device.second)
            {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                std::cout << "Test device: " << std::hex << device.first.first << std::dec << "/" << std::hex << device.first.second << std::dec << " \"" << data.path << "\"";
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
#ifdef __linux__
                hid_device* test_device = hid_open_path(data.path.c_str());
                if (test_device != nullptr)
                {
                    path = data.path;
                    vendor_id = device.first.first;
                    product_id = device.first.second;
                    found = true;
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    std::cout << "-> PASSED" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    hid_close(test_device);
                    break;
                }
#else
                if (data.has_valid_usage())
                {
                    path = data.path;
                    vendor_id = device.first.first;
                    product_id = device.first.second;
                    found = true;
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    std::cout << "-> PASSED" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    break;
                }
#endif // __linux__
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                else
                    std::cout << "-> NOT PASSED" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            }

            if (found)
                break;
        }
    }

    if (path.empty())
    {
        if ((vendor_id != 0) && (product_id != 0))
        {
            // Open the 3Dconnexion device using vendor_id and product_id
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << std::endl << "Opening device: " << std::hex << vendor_id << std::dec << "/" << std::hex << product_id << std::dec << " using hid_open()" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            m_device = hid_open(vendor_id, product_id, nullptr);
        }
        else
            return false;
    }
    else
    {
        // Open the 3Dconnexion device using the device path
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        std::cout << std::endl << "Opening device: " << std::hex << vendor_id << std::dec << "/" << std::hex << product_id << std::dec << "\"" << path << "\" using hid_open_path()" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        m_device = hid_open_path(path.c_str());
    }

    if (m_device != nullptr)
    {
        std::vector<wchar_t> manufacturer(1024, 0);
        hid_get_manufacturer_string(m_device, manufacturer.data(), 1024);
        m_device_str = boost::nowide::narrow(manufacturer.data());

        std::vector<wchar_t> product(1024, 0);
        hid_get_product_string(m_device, product.data(), 1024);
        m_device_str += "/" + boost::nowide::narrow(product.data());

        BOOST_LOG_TRIVIAL(info) << "Connected device: " << m_device_str;

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        std::cout << std::endl << "Connected device:" << std::endl;
        std::cout << "Manufacturer/product: " << m_device_str << std::endl;
        std::cout << "Manufacturer id.....: " << vendor_id << " (" << std::hex << vendor_id << std::dec << ")" << std::endl;
        std::cout << "Product id..........: " << product_id << " (" << std::hex << product_id << std::dec << ")" << std::endl;
        std::cout << "Path................: '" << path << "'" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

        // get device parameters from the config, if present
        double translation_speed = 1.0;
        float rotation_speed = 1.0;
        double translation_deadzone = State::DefaultTranslationDeadzone;
        float rotation_deadzone = State::DefaultRotationDeadzone;
        wxGetApp().app_config->get_mouse_device_translation_speed(m_device_str, translation_speed);
        wxGetApp().app_config->get_mouse_device_translation_deadzone(m_device_str, translation_deadzone);
        wxGetApp().app_config->get_mouse_device_rotation_speed(m_device_str, rotation_speed);
        wxGetApp().app_config->get_mouse_device_rotation_deadzone(m_device_str, rotation_deadzone);
        // clamp to valid values
        m_state.set_translation_scale(State::DefaultTranslationScale * std::max(0.5, std::min(2.0, translation_speed)));
        m_state.set_translation_deadzone(std::max(0.0, std::min(State::MaxTranslationDeadzone, translation_deadzone)));
        m_state.set_rotation_scale(State::DefaultRotationScale * std::max(0.5f, std::min(2.0f, rotation_speed)));
        m_state.set_rotation_deadzone(std::max(0.0f, std::min(State::MaxRotationDeadzone, rotation_deadzone)));
    }
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    else
    {
        std::cout << std::endl << "Unable to connect to device:" << std::endl;
        std::cout << "Manufacturer/product: " << m_device_str << std::endl;
        std::cout << "Manufacturer id.....: " << vendor_id << " (" << std::hex << vendor_id << std::dec << ")" << std::endl;
        std::cout << "Product id..........: " << product_id << " (" << std::hex << product_id << std::dec << ")" << std::endl;
        std::cout << "Path................: '" << path << "'" << std::endl;
    }
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

    return (m_device != nullptr);
}

void Mouse3DController::disconnect_device()
{
    if (!is_device_connected())
        return;
    
    // Stop the secondary thread, if running
    if (m_thread.joinable())
        m_thread.join();

    // Store current device parameters into the config
    wxGetApp().app_config->set_mouse_device(m_device_str, m_state.get_translation_scale() / State::DefaultTranslationScale, m_state.get_translation_deadzone(),
        m_state.get_rotation_scale() / State::DefaultRotationScale, m_state.get_rotation_deadzone());
    wxGetApp().app_config->save();

    // Close the 3Dconnexion device
    hid_close(m_device);
    m_device = nullptr;

    BOOST_LOG_TRIVIAL(info) << "Disconnected device: " << m_device_str;

    m_device_str = "";
}

void Mouse3DController::start()
{
    if (!is_device_connected() || m_running)
        return;

    m_thread = std::thread(&Mouse3DController::run, this);
}

void Mouse3DController::run()
{
    m_running = true;
    while (m_running)
    {
        collect_input();
    }
}

void Mouse3DController::collect_input()
{
    DataPacket packet = { 0 };
    int res = hid_read_timeout(m_device, packet.data(), packet.size(), 100);
    if (res < 0)
    {
        // An error occourred (device detached from pc ?)
        stop();
        return;
    }

    if (!wxGetApp().IsActive())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    bool updated = false;

    if (res == 7)
        updated = handle_packet(packet);
    else if (res == 13)
        updated = handle_wireless_packet(packet);
    else if ((res == 3) && (packet[0] == 3))
        // On Mac button packets can be 3 bytes long
        updated = handle_packet(packet);
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    else if (res > 0)
        std::cout << "Got unknown data packet of length: " << res << ", code:" << (int)packet[0] << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

    if (updated)
        // ask for an idle event to update 3D scene
        wxWakeUpIdle();
}

bool Mouse3DController::handle_packet(const DataPacket& packet)
{
    switch (packet[0])
    {
    case 1: // Translation
        {
            if (handle_packet_translation(packet))
                return true;

            break;
        }
    case 2: // Rotation
        {
            if (handle_packet_rotation(packet, 1))
                return true;

            break;
        }
    case 3: // Button
        {
            if (handle_packet_button(packet, packet.size() - 1))
                return true;

            break;
        }
    case 23: // Battery charge
        {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << m_device_str << " - battery level: " << (int)packet[1] << " percent" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            break;
        }
    default:
        {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << "Got unknown data packet of code: " << (int)packet[0] << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            break;
        }
    }

    return false;
}

bool Mouse3DController::handle_wireless_packet(const DataPacket& packet)
{
    switch (packet[0])
    {
    case 1: // Translation + Rotation
        {
            bool updated = handle_packet_translation(packet);
            updated |= handle_packet_rotation(packet, 7);

            if (updated)
                return true;

            break;
        }
    case 3: // Button
        {
            if (handle_packet_button(packet, 12))
                return true;

            break;
        }
    case 23: // Battery charge
        {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << m_device_str << " - battery level: " << (int)packet[1] << " percent" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            break;
        }
    default:
        {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << "Got unknown data packet of code: " << (int)packet[0] << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            break;
        }
    }
    
    return false;
}

double convert_input(unsigned char first, unsigned char second, double deadzone)
{
    short value = first | second << 8;
    double ret = (double)value / 350.0;
    return (std::abs(ret) > deadzone) ? ret : 0.0;
}

bool Mouse3DController::handle_packet_translation(const DataPacket& packet)
{
    double deadzone = m_state.get_translation_deadzone();
    Vec3d translation(-convert_input(packet[1], packet[2], deadzone),
        convert_input(packet[3], packet[4], deadzone),
        convert_input(packet[5], packet[6], deadzone));

    if (!translation.isApprox(Vec3d::Zero()))
    {
        m_state.append_translation(translation);
        return true;
    }

    return false;
}

bool Mouse3DController::handle_packet_rotation(const DataPacket& packet, unsigned int first_byte)
{
    double deadzone = (double)m_state.get_rotation_deadzone();
    Vec3f rotation(-(float)convert_input(packet[first_byte + 0], packet[first_byte + 1], deadzone),
        (float)convert_input(packet[first_byte + 2], packet[first_byte + 3], deadzone),
        -(float)convert_input(packet[first_byte + 4], packet[first_byte + 5], deadzone));

    if (!rotation.isApprox(Vec3f::Zero()))
    {
        m_state.append_rotation(rotation);
        return true;
    }

    return false;
}

bool Mouse3DController::handle_packet_button(const DataPacket& packet, unsigned int packet_size)
{
    unsigned int data = 0;
    for (unsigned int i = 1; i < packet_size; ++i)
    {
        data |= packet[i] << 8 * (i - 1);
    }

    const std::bitset<32> data_bits{ data };
    for (size_t i = 0; i < data_bits.size(); ++i)
    {
        if (data_bits.test(i))
        {
            m_state.append_button((unsigned int)i);
            return true;
        }
    }

    return false;
}

} // namespace GUI
} // namespace Slic3r
