#ifndef slic3r_GLGizmoSimplify_hpp_
#define slic3r_GLGizmoSimplify_hpp_

// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code,
// which overrides our localization "L" macro.
#include "GLGizmoBase.hpp"
#include "GLGizmoPainterBase.hpp" // for render wireframe
#include "slic3r/GUI/GLModel.hpp"
#include "admesh/stl.h" // indexed_triangle_set
#include <thread>
#include <mutex>
#include <optional>
#include <atomic>

#include <GL/glew.h> // GLUint

// for simplify suggestion
class ModelObjectPtrs; //  std::vector<ModelObject*>

namespace Slic3r {
class ModelVolume;

namespace GUI {
class NotificationManager; // for simplify suggestion

class GLGizmoSimplify: public GLGizmoBase
{    
public:
    GLGizmoSimplify(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    virtual ~GLGizmoSimplify();
    bool on_esc_key_down();
    static void add_simplify_suggestion_notification(
        const std::vector<size_t> &object_ids,
        const ModelObjectPtrs &    objects,
        NotificationManager &      manager);

protected:
    virtual std::string on_get_name() const override;
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;
    virtual bool on_is_activable() const override;
    virtual bool on_is_selectable() const override { return false; }
    virtual void on_set_state() override;

    // must implement
    virtual bool on_init() override { return true;};
    virtual void on_render() override;
    virtual void on_render_for_picking() override{};    

    virtual CommonGizmosDataID on_get_requirements() const;

private:
    void after_apply();
    void close();
    void live_preview();
    void process();
    void set_its(const indexed_triangle_set &its);
    void create_gui_cfg();
    void request_rerender();

    void set_center_position();
    // move to global functions
    static ModelVolume *get_volume(const Selection &selection, Model &model);
    static const ModelVolume *get_volume(const GLVolume::CompositeID &cid, const Model &model);

    // return false when volume was deleted
    static bool exist_volume(const ModelVolume *volume);

    std::atomic_bool m_is_valid_result; // differ what to do in apply
    std::atomic_bool m_exist_preview;   // set when process end

    bool m_move_to_center; // opening gizmo

    
    ModelVolume *m_volume; // keep pointer to actual working volume
    size_t m_obj_index;

    std::optional<indexed_triangle_set> m_original_its;
    bool m_show_wireframe;
    GLModel m_glmodel;

    std::atomic<bool> m_need_reload; // after simplify, glReload must be on main thread

    std::thread m_worker;
    std::mutex m_state_mutex;

    struct State {
        enum Status {
            settings,
            preview,      // simplify to show preview
            close_on_end, // simplify with close on end
            canceling // after button click, before canceled
        };

        Status status;
        int progress; // percent of done work
        indexed_triangle_set result;
    };
    
    State m_state;

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
            if (decimate_ratio <= 0.f) 
                wanted_count = static_cast<uint32_t>(triangle_count);
            else if (decimate_ratio >= 100.f)
                wanted_count = 0;
            else
                wanted_count = static_cast<uint32_t>(std::round(
                    triangle_count * (100.f - decimate_ratio) / 100.f));
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

        // to prevent freezing when move in gui
        // delay before process in [ms]
        std::chrono::duration<long int, std::milli> prcess_delay = std::chrono::milliseconds(250);
    };
    std::optional<GuiCfg> m_gui_cfg;

    // translations used for calc window size
    const std::string tr_mesh_name;
    const std::string tr_triangles;
    const std::string tr_preview;
    const std::string tr_detail_level;
    const std::string tr_decimate_ratio;

    void init_model();

    // cancel exception
    class SimplifyCanceledException: public std::exception
    {
    public:
        const char *what() const throw()
        {
            return L("Model simplification has been canceled");
        }
    };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoSimplify_hpp_
