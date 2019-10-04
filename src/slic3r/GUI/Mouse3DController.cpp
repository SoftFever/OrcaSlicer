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

static const std::vector<int> _3DCONNEXION_DEVICES =
{
    0xC623, // TRAVELER = 50723
    0xC626, // NAVIGATOR = 50726
    0xc628,	// NAVIGATOR_FOR_NOTEBOOKS = 50728
    0xc627, // SPACEEXPLORER = 50727
    0xC603, // SPACEMOUSE = 50691
    0xC62B, // SPACEMOUSEPRO = 50731
    0xc621, // SPACEBALL5000 = 50721
    0xc625, // SPACEPILOT = 50725
    0xc629  // SPACEPILOTPRO = 50729
};

namespace Slic3r {
namespace GUI {
    
const double Mouse3DController::State::DefaultTranslationScale = 2.5;
const float Mouse3DController::State::DefaultRotationScale = 1.0;

Mouse3DController::State::State()
    : m_translation(Vec3d::Zero())
    , m_rotation(Vec3f::Zero())
    , m_translation_scale(DefaultTranslationScale)
    , m_rotation_scale(DefaultRotationScale)
{
}

bool Mouse3DController::State::apply(Camera& camera)
{
    if (!wxGetApp().IsActive())
        return false;

    bool ret = false;

    if (has_translation())
    {
        camera.set_target(camera.get_target() + m_translation_scale * (m_translation(0) * camera.get_dir_right() + m_translation(1) * camera.get_dir_forward() + m_translation(2) * camera.get_dir_up()));
        m_translation = Vec3d::Zero();
        ret = true;
    }

    if (has_rotation())
    {
        float theta = m_rotation_scale * m_rotation(0);
        float phi = m_rotation_scale * m_rotation(2);
        float sign = camera.inverted_phi ? -1.0f : 1.0f;
        camera.phi += sign * phi;
        camera.set_theta(camera.get_theta() + theta, wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA);
        m_rotation = Vec3f::Zero();
        ret = true;
    }

    if (has_any_button())
    {
        for (unsigned int i : m_buttons)
        {
            switch (i)
            {
            case 0: { camera.update_zoom(1.0); break; }
            case 1: { camera.update_zoom(-1.0); break; }
            default: { break; }
            }
        }

        reset_buttons();
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

    float rotation = (float)m_state.get_rotation_scale() / State::DefaultRotationScale;
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
    auto convert_input = [](unsigned char first, unsigned char second)-> double
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

    // Read data from device
    enum EDataType
    {
        Translation = 1,
        Rotation,
        Button
    };

    unsigned char retrieved_data[8] = { 0 };
    int res = hid_read_timeout(m_device, retrieved_data, sizeof(retrieved_data), 100);
    if (res < 0)
    {
        // An error occourred (device detached from pc ?)
        stop();
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (res > 0)
    {
        switch (retrieved_data[0])
        {
        case Translation:
            {
                Vec3d translation(-convert_input(retrieved_data[1], retrieved_data[2]),
                    convert_input(retrieved_data[3], retrieved_data[4]),
                    convert_input(retrieved_data[5], retrieved_data[6]));
            if (!translation.isApprox(Vec3d::Zero()))
                    m_state.set_translation(translation);

                break;
            }
        case Rotation:
            {
                Vec3f rotation(-(float)convert_input(retrieved_data[1], retrieved_data[2]),
                    (float)convert_input(retrieved_data[3], retrieved_data[4]),
                    -(float)convert_input(retrieved_data[5], retrieved_data[6]));
                if (!rotation.isApprox(Vec3f::Zero()))
                    m_state.set_rotation(rotation);

                break;
            }
        case Button:
            {
                // Because of lack of documentation, it is not clear how we should interpret the retrieved data for the button case.
                // Experiments made with SpaceNavigator:
                // retrieved_data[1] == 0 if no button pressed
                // retrieved_data[1] == 1 if left button pressed
                // retrieved_data[1] == 2 if right button pressed
                // retrieved_data[1] == 3 if left and right button pressed
                // seems to show that each button is associated to a bit of retrieved_data[1], which means that at max 8 buttons can be supported.
                for (unsigned int i = 0; i < 8; ++i)
                {
                    if (retrieved_data[1] & (0x1 << i))
                        m_state.set_button(i);
                }

//                // On the other hand, other libraries, as in https://github.com/koenieee/CrossplatformSpacemouseDriver/blob/master/SpaceMouseDriver/driver/SpaceMouseController.cpp
//                // interpret retrieved_data[1] as the button id
//                if (retrieved_data[1] > 0)
//                    m_state.set_button((unsigned int)retrieved_data[1]);

                break;
            }
        default:
            break;
        }
    }
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_3DCONNEXION_DEVICES
