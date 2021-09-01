#ifndef slic3r_GLGizmoSimplify_hpp_
#define slic3r_GLGizmoSimplify_hpp_

// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code,
// which overrides our localization "L" macro.
#include "GLGizmoBase.hpp"
#include "admesh/stl.h" // indexed_triangle_set
#include <thread>
#include <optional>
#include <atomic>

namespace Slic3r {

class ModelVolume;

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
    void after_apply();
    void close();
    void process();
    void set_its(indexed_triangle_set &its);
    void create_gui_cfg();
    void request_rerender();
    bool is_selected_object(int *object_idx = nullptr);

    std::atomic_bool m_is_valid_result; // differ what to do in apply
    std::atomic_bool m_exist_preview;   // set when process end

    volatile int m_progress; // percent of done work
    ModelVolume *m_volume; // 
    size_t m_obj_index;

    std::optional<indexed_triangle_set> m_original_its;

    volatile bool m_need_reload; // after simplify, glReload must be on main thread
    std::thread m_worker;

    enum class State {
        settings,
        preview,      // simplify to show preview
        close_on_end, // simplify with close on end
        canceling // after button click, before canceled
    };
    volatile State m_state;

    struct Configuration
    {
        bool use_count = false;
        // minimal triangle count
        float    decimate_ratio = 50.f; // in percent
        uint32_t wanted_count   = 0; // initialize by percents

        // maximal quadric error
        float max_error = 1.;

        void fix_count_by_ratio(size_t triangle_count)
        {
            wanted_count = static_cast<uint32_t>(
                std::round(triangle_count * (100.f-decimate_ratio) / 100.f));
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
        int window_offset_x   = 100;
        int window_offset_y   = 100;
        int window_padding    = 0;

        // trunc model name when longer
        size_t max_char_in_name = 30;
    };
    std::optional<GuiCfg> m_gui_cfg;

    // translations used for calc window size
    const std::string tr_mesh_name;
    const std::string tr_triangles;
    const std::string tr_preview;
    const std::string tr_detail_level;
    const std::string tr_decimate_ratio;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoSimplify_hpp_
