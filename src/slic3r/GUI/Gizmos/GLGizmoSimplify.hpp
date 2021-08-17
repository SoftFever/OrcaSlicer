#ifndef slic3r_GLGizmoSimplify_hpp_
#define slic3r_GLGizmoSimplify_hpp_

#include "GLGizmoBase.hpp"
#include "libslic3r/Model.hpp"
#include <thread>
#include <optional>

namespace Slic3r {
namespace GUI {

class GLGizmoSimplify : public GLGizmoBase
{    
public:
    GLGizmoSimplify(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    virtual ~GLGizmoSimplify();
protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;    
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;
    virtual bool on_is_activable() const override;
    virtual bool on_is_selectable() const override { return false; }
    virtual void on_set_state() override;

private:
    void close();
    void process();
    void set_its(indexed_triangle_set &its);
    void create_gui_cfg();

    bool m_is_valid_result; // differ what to do in apply
    volatile int m_progress; // percent of done work
    ModelVolume *m_volume; // 
    size_t m_obj_index;

    std::optional<indexed_triangle_set> m_original_its;
    std::optional<float> m_last_error; // for use previous reduction

    volatile bool m_need_reload; // after simplify, glReload must be on main thread
    std::thread m_worker;

    enum class State {
        settings,
        simplifying, // start processing
        canceling, // canceled
        successfull, // successful simplified
        close_on_end
    };
    volatile State m_state;

    struct Configuration
    {
        bool use_count = true;
        // minimal triangle count
        float    wanted_percent = 50.f;
        uint32_t wanted_count   = 0; // initialize by percents

        bool use_error = false;
        // maximal quadric error
        float max_error = 1.;

        void update_count(size_t triangle_count)
        {
            wanted_percent = (float) wanted_count / triangle_count * 100.f;
        }
        void update_percent(size_t triangle_count)
        {
            wanted_count = static_cast<uint32_t>(
                std::round(triangle_count * wanted_percent / 100.f));
        }
    } m_configuration;

    // This configs holds GUI layout size given by translated texts.
    // etc. When language changes, GUI is recreated and this class constructed again,
    // so the change takes effect. (info by GLGizmoFdmSupports.hpp)
    struct GuiCfg
    {
        int top_left_width    = 100;
        int bottom_left_width = 100;
        int input_width       = 100;
        int input_small_width = 80;
        int window_offset     = 100;
        int window_padding    = 0;
    };
    std::optional<GuiCfg> m_gui_cfg;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoSimplify_hpp_
