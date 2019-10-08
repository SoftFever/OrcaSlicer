#ifndef slic3r_Mouse3DController_hpp_
#define slic3r_Mouse3DController_hpp_

#if ENABLE_3DCONNEXION_DEVICES
#include "libslic3r/Point.hpp"

#include "hidapi.h"

#include <queue>
#include <thread>
#include <mutex>
#include <vector>

namespace Slic3r {
namespace GUI {

struct Camera;

class Mouse3DController
{
    class State
    {
    public:
        static const double DefaultTranslationScale;
        static const float DefaultRotationScale;

    private:
        std::queue<Vec3d> m_translation;
        std::queue<Vec3f> m_rotation;
        std::queue<unsigned int> m_buttons;

        double m_translation_scale;
        float m_rotation_scale;

        // When the 3Dconnexion driver is running the system gets, by default, mouse wheel events when rotations around the X axis are detected.
        // We want to filter these out because we are getting the data directly from the device, bypassing the driver, and those mouse wheel events interfere
        // by triggering unwanted zoom in/out of the scene
        // The following variable is used to count the potential mouse wheel events triggered and is updated by: 
        // Mouse3DController::collect_input() through the call to the append_rotation() method
        // GLCanvas3D::on_mouse_wheel() through the call to the process_mouse_wheel() method
        // GLCanvas3D::on_idle() through the call to the apply() method
        unsigned int m_mouse_wheel_counter;

    public:
        State();

        void append_translation(const Vec3d& translation) { m_translation.push(translation); }
        void append_rotation(const Vec3f& rotation)
        {
            m_rotation.push(rotation);
            if (rotation(0) != 0.0f)
                ++m_mouse_wheel_counter;
        }
        void append_button(unsigned int id) { m_buttons.push(id); }

        bool has_translation() const { return !m_translation.empty(); }
        bool has_rotation() const { return !m_rotation.empty(); }
        bool has_any_button() const { return !m_buttons.empty(); }

        bool process_mouse_wheel();

        double get_translation_scale() const { return m_translation_scale; }
        void set_translation_scale(double scale) { m_translation_scale = scale; }

        float get_rotation_scale() const { return m_rotation_scale; }
        void set_rotation_scale(float scale) { m_rotation_scale = scale; }

        // return true if any change to the camera took place
        bool apply(Camera& camera);
    };

    bool m_initialized;
    mutable State m_state;
    std::mutex m_mutex;
    std::thread m_thread;
    hid_device* m_device;
    std::string m_device_str;
    bool m_running;
    bool m_settings_dialog;

public:
    Mouse3DController();

    void init();
    void shutdown();

    bool is_device_connected() const { return m_device != nullptr; }
    bool is_running() const { return m_running; }

    bool process_mouse_wheel() { std::lock_guard<std::mutex> lock(m_mutex); return m_state.process_mouse_wheel(); }

    bool apply(Camera& camera);

    bool is_settings_dialog_shown() const { return m_settings_dialog; }
    void show_settings_dialog(bool show) { m_settings_dialog = show && is_running(); }
    void render_settings_dialog(unsigned int canvas_width, unsigned int canvas_height) const;

private:
    bool connect_device();
    void disconnect_device();
    void start();
    void stop() { m_running = false; }

    // secondary thread methods
    void run();
    void collect_input();
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_3DCONNEXION_DEVICES

#endif // slic3r_Mouse3DController_hpp_

