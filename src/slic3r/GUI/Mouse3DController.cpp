#include "libslic3r/libslic3r.h"
#include "Mouse3DController.hpp"

#if ENABLE_3DCONNEXION_DEVICES

#include "Camera.hpp"
#include "GUI_App.hpp"
#include "PresetBundle.hpp"
#include "AppConfig.hpp"

#include <wx/glcanvas.h>

#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>
#include "I18N.hpp"

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
    0xc62e,	/* 50734 spacemouse wireless (USB cable) */
    0xc62f,	/* 50735 spacemouse wireless  receiver */
    0xc631,	/* 50737 spacemouse pro wireless *TESTED* */
    0xc632,	/* 50738 spacemouse pro wireless receiver */
    0xc633,	/* 50739 spacemouse enterprise */
    0xc635,	/* 50741 spacemouse compact */
    0xc636,	/* 50742 spacemouse module */
    0xc640,	/* 50752 nulooq */

//    0xc652, /* 50770 3Dconnexion universal receiver */ 
};

namespace Slic3r {
namespace GUI {
    
const double Mouse3DController::State::DefaultTranslationScale = 2.5;
const float Mouse3DController::State::DefaultRotationScale = 1.0f;

Mouse3DController::State::State()
    : m_translation_scale(DefaultTranslationScale)
    , m_rotation_scale(DefaultRotationScale)
    , m_mouse_wheel_counter(0)
{
}

bool Mouse3DController::State::process_mouse_wheel()
{
    if (m_mouse_wheel_counter == 0)
        return false;
    else if (!m_rotation.empty())
    {
        --m_mouse_wheel_counter;
        return true;
    }
    else
    {
        m_mouse_wheel_counter = 0;
        return true;
    }
}

bool Mouse3DController::State::apply(Camera& camera)
{
    if (!wxGetApp().IsActive())
        return false;

    bool ret = false;

    if (has_translation())
    {
        const Vec3d& translation = m_translation.front();
        camera.set_target(camera.get_target() + m_translation_scale * (translation(0) * camera.get_dir_right() + translation(1) * camera.get_dir_forward() + translation(2) * camera.get_dir_up()));
        m_translation.pop();
        ret = true;
    }

    if (has_rotation())
    {
        const Vec3f& rotation = m_rotation.front();
        float theta = m_rotation_scale * rotation(0);
        float phi = m_rotation_scale * rotation(2);
        float sign = camera.inverted_phi ? -1.0f : 1.0f;
        camera.phi += sign * phi;
        camera.set_theta(camera.get_theta() + theta, wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA);
        m_rotation.pop();

        ret = true;
    }

    if (has_any_button())
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
    , m_settings_dialog(false)
{
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
        // hides the settings dialog if the user re-plug the device
        m_settings_dialog = false;
    }

    // check if the user plugged the device
    if (connect_device())
        start();

    return is_device_connected() ? m_state.apply(camera) : false;
}

void Mouse3DController::render_settings_dialog(unsigned int canvas_width, unsigned int canvas_height) const
{
    if (!m_running || !m_settings_dialog)
        return;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    imgui.set_next_window_pos(0.5f * (float)canvas_width, 0.5f * (float)canvas_height, ImGuiCond_Always, 0.5f, 0.5f);
    imgui.set_next_window_bg_alpha(0.5f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    imgui.begin(_(L("3Dconnexion settings")), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

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

    float translation = (float)m_state.get_translation_scale() / State::DefaultTranslationScale;
    if (ImGui::SliderFloat(_(L("Translation")), &translation, 0.5f, 2.0f, "%.1f"))
        m_state.set_translation_scale(State::DefaultTranslationScale * (double)translation);

    float rotation = m_state.get_rotation_scale() / State::DefaultRotationScale;
    if (ImGui::SliderFloat(_(L("Rotation")), &rotation, 0.5f, 2.0f, "%.1f"))
        m_state.set_rotation_scale(State::DefaultRotationScale * rotation);

    imgui.end();

    ImGui::PopStyleVar();
}

bool Mouse3DController::connect_device()
{
    if (is_device_connected())
        return false;

    // Enumerates devices
    hid_device_info* devices = hid_enumerate(0, 0);
    if (devices == nullptr)
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to enumerate HID devices";
        return false;
    }

    // Searches for 1st connected 3Dconnexion device
    unsigned short vendor_id = 0;
    unsigned short product_id = 0;

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    hid_device_info* cur = devices;
    while (cur != nullptr)
    {
        std::cout << "Detected device '";

        if (cur->manufacturer_string != nullptr)
            std::wcout << cur->manufacturer_string << L" ";
        else
            std::wcout << "MMM ";
        if (cur->product_string != nullptr)
            std::wcout << cur->product_string;
        else
            std::wcout << "PPP";
        std::cout << "' code: " << cur->product_id << " (" << std::hex << cur->product_id << std::dec << ")" << std::endl;

        cur = cur->next;
    }
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    hid_device_info* current = devices;
    while (current != nullptr)
    {
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
                    break;
                }
            }

            if (product_id == 0)
                vendor_id = 0;
        }

        if (vendor_id != 0)
            break;

        current = current->next;
    }

    // Free enumerated devices
    hid_free_enumeration(devices);

    if (vendor_id == 0)
        return false;

    // Open the 3Dconnexion device using the VID, PID
    m_device = hid_open(vendor_id, product_id, nullptr);

    if (m_device != nullptr)
    {
        std::vector<wchar_t> product(1024, 0);
        hid_get_product_string(m_device, product.data(), 1024);
        m_device_str = boost::nowide::narrow(product.data());

        BOOST_LOG_TRIVIAL(info) << "Connected device: " << m_device_str;

        // get device parameters from the config, if present
        double translation = 1.0;
        float rotation = 1.0;
        wxGetApp().app_config->get_mouse_device_translation_speed(m_device_str, translation);
        wxGetApp().app_config->get_mouse_device_rotation_speed(m_device_str, rotation);
        // clamp to valid values
        m_state.set_translation_scale(State::DefaultTranslationScale * std::max(0.5, std::min(2.0, translation)));
        m_state.set_rotation_scale(State::DefaultRotationScale * std::max(0.5f, std::min(2.0f, rotation)));
    }

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
    wxGetApp().app_config->set_mouse_device(m_device_str, m_state.get_translation_scale() / State::DefaultTranslationScale, m_state.get_rotation_scale() / State::DefaultRotationScale);
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
    else if (res > 0)
        std::cout << "Got unknown data packet of length: " << res << ", code:" << (int)packet[0] << std::endl;

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
            if (handle_packet_button(packet, 6))
                return true;

            break;
        }
    default:
        {
            std::cout << "Got unknown data packet of code: " << (int)packet[0] << std::endl;
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
    default:
        {
            std::cout << "Got unknown data packet of code: " << (int)packet[0] << std::endl;
            break;
        }
    }
    
    return false;
}

double convert_input(unsigned char first, unsigned char second)
{
    int ret = 0;
    switch (second)
    {
    case 0: { ret = (int)first; break; }
    case 1: { ret = (int)first + 255; break; }
    case 254: { ret = -511 + (int)first; break; }
    case 255: { ret = -255 + (int)first; break; }
    default: { break; }
    }
    return (double)ret / 349.0;
};

bool Mouse3DController::handle_packet_translation(const DataPacket& packet)
{
    Vec3d translation(-convert_input(packet[1], packet[2]),
        convert_input(packet[3], packet[4]),
        convert_input(packet[5], packet[6]));
    if (!translation.isApprox(Vec3d::Zero()))
    {
        m_state.append_translation(translation);
        return true;
    }

    return false;
}

bool Mouse3DController::handle_packet_rotation(const DataPacket& packet, unsigned int first_byte)
{
    Vec3f rotation(-(float)convert_input(packet[first_byte + 0], packet[first_byte + 1]),
        (float)convert_input(packet[first_byte + 2], packet[first_byte + 3]),
        -(float)convert_input(packet[first_byte + 4], packet[first_byte + 5]));
    if (!rotation.isApprox(Vec3f::Zero()))
    {
        m_state.append_rotation(rotation);
        return true;
    }

    return false;
}

bool Mouse3DController::handle_packet_button(const DataPacket& packet, unsigned int packet_size)
{
    for (unsigned int i = 0, base = 0; i < packet_size; ++i, base += 8)
    {
        for (unsigned int j = 0; j < 8; ++j)
        {
            if (packet[i + 1] & (0x1 << j))
            {
                m_state.append_button(base + j);
                return true;
            }
        }
    }

    return false;
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_3DCONNEXION_DEVICES
