#ifndef slic3r_GLToolbar_hpp_
#define slic3r_GLToolbar_hpp_

#include <functional>
#include <string>
#include <vector>

#include "GLTexture.hpp"
#include "Event.hpp"
#include "libslic3r/Point.hpp"

class wxEvtHandler;

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

wxDECLARE_EVENT(EVT_GLTOOLBAR_ADD, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_DELETE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_DELETE_ALL, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_ARRANGE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_COPY, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_PASTE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_MORE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_FEWER, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SPLIT_OBJECTS, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SPLIT_VOLUMES, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_LAYERSEDITING, SimpleEvent);

wxDECLARE_EVENT(EVT_GLVIEWTOOLBAR_3D, SimpleEvent);
wxDECLARE_EVENT(EVT_GLVIEWTOOLBAR_PREVIEW, SimpleEvent);

class GLToolbarItem
{
public:
    typedef std::function<void()> ActionCallback;
    typedef std::function<bool()> VisibilityCallback;
    typedef std::function<bool()> EnabledStateCallback;
    typedef std::function<void(float, float,float, float)> RenderCallback;

    enum EType : unsigned char
    {
        Action,
        Separator,
        Num_Types
    };

    enum EState : unsigned char
    {
        Normal,
        Pressed,
        Disabled,
        Hover,
        HoverPressed,
        Num_States
    };

    struct Data
    {
        std::string name;
#if ENABLE_SVG_ICONS
        std::string icon_filename;
#endif // ENABLE_SVG_ICONS
        std::string tooltip;
        unsigned int sprite_id;
        bool is_toggable;
        bool visible;
        ActionCallback action_callback;
        VisibilityCallback visibility_callback;
        EnabledStateCallback enabled_state_callback;
        RenderCallback render_callback;

        Data();
    };

    static const ActionCallback Default_Action_Callback;
    static const VisibilityCallback Default_Visibility_Callback;
    static const EnabledStateCallback Default_Enabled_State_Callback;
    static const RenderCallback Default_Render_Callback;

private:
    EType m_type;
    EState m_state;
    Data m_data;

public:
    GLToolbarItem(EType type, const Data& data);

    EState get_state() const { return m_state; }
    void set_state(EState state) { m_state = state; }

    const std::string& get_name() const { return m_data.name; }
#if ENABLE_SVG_ICONS
    const std::string& get_icon_filename() const { return m_data.icon_filename; }
#endif // ENABLE_SVG_ICONS
    const std::string& get_tooltip() const { return m_data.tooltip; }

    void do_action() { m_data.action_callback(); }

    bool is_enabled() const { return m_state != Disabled; }
    bool is_disabled() const { return m_state == Disabled; }
    bool is_hovered() const { return (m_state == Hover) || (m_state == HoverPressed); }
    bool is_pressed() const { return (m_state == Pressed) || (m_state == HoverPressed); }

    bool is_toggable() const { return m_data.is_toggable; }
    bool is_visible() const { return m_data.visible; }
    bool is_separator() const { return m_type == Separator; }

    // returns true if the state changes
    bool update_visibility();
    // returns true if the state changes
    bool update_enabled_state();

    void render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int tex_width, unsigned int tex_height, unsigned int icon_size) const;

private:
    GLTexture::Quad_UVs get_uvs(unsigned int tex_width, unsigned int tex_height, unsigned int icon_size) const;
    void set_visible(bool visible) { m_data.visible = visible; }

    friend class GLToolbar;
};

#if !ENABLE_SVG_ICONS
// items icon textures are assumed to be square and all with the same size in pixels, no internal check is done
// icons are layed-out into the texture starting from the top-left corner in the same order as enum GLToolbarItem::EState
// from left to right
struct ItemsIconsTexture
{
    struct Metadata
    {
        // path of the file containing the icons' texture
        std::string filename;
        // size of the square icons, in pixels
        unsigned int icon_size;

        Metadata();
    };

    GLTexture texture;
    Metadata metadata;
};
#endif // !ENABLE_SVG_ICONS

struct BackgroundTexture
{
    struct Metadata
    {
        // path of the file containing the background texture
        std::string filename;
        // size of the left edge, in pixels
        unsigned int left;
        // size of the right edge, in pixels
        unsigned int right;
        // size of the top edge, in pixels
        unsigned int top;
        // size of the bottom edge, in pixels
        unsigned int bottom;

        Metadata();
    };

    GLTexture texture;
    Metadata metadata;
};

class GLToolbar
{
public:
#if ENABLE_SVG_ICONS
    static const float Default_Icons_Size;
#endif // ENABLE_SVG_ICONS

    enum EType : unsigned char
    {
        Normal,
        Radio,
        Num_Types
    };

    struct Layout
    {
        enum EType : unsigned char
        {
            Horizontal,
            Vertical,
            Num_Types
        };

        enum EOrientation : unsigned int
        {
            Top,
            Bottom,
            Left,
            Right,
            Center,
            Num_Locations
        };

        EType type;
        EOrientation orientation;
        float top;
        float left;
        float border;
        float separator_size;
        float gap_size;
#if ENABLE_SVG_ICONS
        float icons_size;
        float scale;
#else
        float icons_scale;
#endif // ENABLE_SVG_ICONS

        float width;
        float height;
        bool dirty;

        Layout();
    };

private:
    typedef std::vector<GLToolbarItem*> ItemsList;

    EType m_type;
#if ENABLE_SVG_ICONS
    std::string m_name;
#endif // ENABLE_SVG_ICONS
    bool m_enabled;
#if ENABLE_SVG_ICONS
    mutable GLTexture m_icons_texture;
    mutable bool m_icons_texture_dirty;
#else
    ItemsIconsTexture m_icons_texture;
#endif // ENABLE_SVG_ICONS
    BackgroundTexture m_background_texture;
    mutable Layout m_layout;

    ItemsList m_items;

    struct MouseCapture
    {
        bool left;
        bool middle;
        bool right;
        GLCanvas3D* parent;

        MouseCapture() { reset(); }

        bool any() const { return left || middle || right; }
        void reset() { left = middle = right = false; parent = nullptr; }
    };

    MouseCapture m_mouse_capture;
    std::string m_tooltip;
    bool m_undo_imgui_visible {false};
    bool m_redo_imgui_visible {false};
    int  m_imgui_hovered_pos  { -1 };
    int  m_imgui_selected_pos { -1 };

public:
#if ENABLE_SVG_ICONS
    GLToolbar(EType type, const std::string& name);
#else
    explicit GLToolbar(EType type);
#endif // ENABLE_SVG_ICONS
    ~GLToolbar();

#if ENABLE_SVG_ICONS
    bool init(const BackgroundTexture::Metadata& background_texture);
#else
    bool init(const ItemsIconsTexture::Metadata& icons_texture, const BackgroundTexture::Metadata& background_texture);
#endif // ENABLE_SVG_ICONS

    Layout::EType get_layout_type() const;
    void set_layout_type(Layout::EType type);
    Layout::EOrientation get_layout_orientation() const;
    void set_layout_orientation(Layout::EOrientation orientation);

    void set_position(float top, float left);
    void set_border(float border);
    void set_separator_size(float size);
    void set_gap_size(float size);
#if ENABLE_SVG_ICONS
    void set_icons_size(float size);
    void set_scale(float scale);
#else
    void set_icons_scale(float scale);
#endif // ENABLE_SVG_ICONS

    bool is_enabled() const;
    void set_enabled(bool enable);

    bool add_item(const GLToolbarItem::Data& data);
    bool add_separator();

    float get_width() const;
    float get_height() const;

    void select_item(const std::string& name);

    bool is_item_pressed(const std::string& name) const;
    bool is_item_disabled(const std::string& name) const;
    bool is_item_visible(const std::string& name) const;

    const std::string& get_tooltip() const { return m_tooltip; }


    // returns true if any item changed its state
    bool update_items_state();

    void render(const GLCanvas3D& parent) const;    

    bool on_mouse(wxMouseEvent& evt, GLCanvas3D& parent);

    // undo == true  => "undo" imgui is activated
    // undo == false => "redo" imgui is activated
    bool get_imgui_visible(const bool undo) const   { return undo ? m_undo_imgui_visible : m_redo_imgui_visible; }
    void hide_imgui(const bool undo)                { undo ? m_undo_imgui_visible = false : m_redo_imgui_visible = false; }
    void activate_imgui(const bool undo)            {
        m_undo_imgui_visible = undo;
        m_redo_imgui_visible = !undo;
        m_imgui_hovered_pos = m_imgui_selected_pos = -1;
    }

    void set_imgui_hovered_pos(int pos = -1)    { m_imgui_hovered_pos = pos;   }
    int  get_imgui_hovered_pos() const          { return m_imgui_hovered_pos;  }

    void set_imgui_selected_pos(int pos = -1)   { m_imgui_selected_pos = pos;  }
    int  get_imgui_selected_pos() const         { return m_imgui_selected_pos; }

private:
    void calc_layout() const;
    float get_width_horizontal() const;
    float get_width_vertical() const;
    float get_height_horizontal() const;
    float get_height_vertical() const;
    float get_main_size() const;
    void do_action(unsigned int item_id, GLCanvas3D& parent);
    std::string update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent);
    std::string update_hover_state_horizontal(const Vec2d& mouse_pos, GLCanvas3D& parent);
    std::string update_hover_state_vertical(const Vec2d& mouse_pos, GLCanvas3D& parent);
    // returns the id of the item under the given mouse position or -1 if none
    int contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;
    int contains_mouse_horizontal(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;
    int contains_mouse_vertical(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;

    void render_horizontal(const GLCanvas3D& parent) const;
    void render_vertical(const GLCanvas3D& parent) const;

#if ENABLE_SVG_ICONS
    bool generate_icons_texture() const;
#endif // ENABLE_SVG_ICONS

    // returns true if any item changed its state
    bool update_items_visibility();
    // returns true if any item changed its state
    bool update_items_enabled_state();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLToolbar_hpp_
