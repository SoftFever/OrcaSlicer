#ifndef slic3r_Mouse3DController_hpp_
#define slic3r_Mouse3DController_hpp_

#if ENABLE_3DCONNEXION_DEVICES
#include "libslic3r/Point.hpp"

#include "hidapi.h"

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
        Vec3d m_translation;
        Vec3f m_rotation;
        std::vector<unsigned int> m_buttons;

        double m_translation_scale;
        float m_rotation_scale;

    public:
        State();

        void set_translation(const Vec3d& translation) { m_translation = translation; }
        void set_rotation(const Vec3f& rotation) { m_rotation = rotation; }
        void set_button(unsigned int id) { m_buttons.push_back(id); }
        void reset_buttons() { return m_buttons.clear(); }

        const Vec3d& get_translation() const { return m_translation; }
        const Vec3f& get_rotation() const { return m_rotation; }
        const std::vector<unsigned int>& get_buttons() const { return m_buttons; }

        bool has_translation() const { return !m_translation.isApprox(Vec3d::Zero()); }
        bool has_rotation() const { return !m_rotation.isApprox(Vec3f::Zero()); }
        bool has_translation_or_rotation() const { return !m_translation.isApprox(Vec3d::Zero()) && !m_rotation.isApprox(Vec3f::Zero()); }
        bool has_any_button() const { return !m_buttons.empty(); }

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

    bool has_translation() const { return m_state.has_translation(); }
    bool has_rotation() const { return m_state.has_rotation(); }
    bool has_translation_or_rotation() const { return m_state.has_translation_or_rotation(); }
    bool has_any_button() const { return m_state.has_any_button(); }

    bool apply(Camera& camera);

    bool is_settings_dialog_shown() const { return m_settings_dialog; }
    void show_settings_dialog(bool show) { m_settings_dialog = show; }
    void render_settings_dialog(unsigned int canvas_width, unsigned int canvas_height) const;

private:
    void connect_device();
    void disconnect_device();
    void start();
    void stop() { m_running = false; }

    void run();
    void collect_input();
};

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_3DCONNEXION_DEVICES

#endif // slic3r_Mouse3DController_hpp_

