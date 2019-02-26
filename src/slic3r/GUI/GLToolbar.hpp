#ifndef slic3r_GLToolbar_hpp_
#define slic3r_GLToolbar_hpp_

#include <functional>
#include <string>
#include <vector>

#include "GLTexture.hpp"
#include "Event.hpp"

class wxEvtHandler;

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

wxDECLARE_EVENT(EVT_GLTOOLBAR_ADD, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_DELETE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_DELETE_ALL, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_ARRANGE, SimpleEvent);
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
        std::string svg_file;
#endif // ENABLE_SVG_ICONS
        std::string tooltip;
        unsigned int sprite_id;
        bool is_toggable;
        wxEventType action_event;
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
        bool visible;
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

        Data();
    };

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
    const std::string& get_svg_file() const { return m_data.svg_file; }
#endif // ENABLE_SVG_ICONS
    const std::string& get_tooltip() const { return m_data.tooltip; }

    void do_action(wxEvtHandler *target);

    bool is_enabled() const { return m_state != Disabled; }
    bool is_disabled() const { return m_state == Disabled; }
    bool is_hovered() const { return (m_state == Hover) || (m_state == HoverPressed); }
    bool is_pressed() const { return (m_state == Pressed) || (m_state == HoverPressed); }

    bool is_toggable() const { return m_data.is_toggable; }
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
    bool is_visible() const { return m_data.visible; }
    void set_visible(bool visible) { m_data.visible = visible; }
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS
    bool is_separator() const { return m_type == Separator; }

    void render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int texture_size, unsigned int icon_size) const;

private:
    GLTexture::Quad_UVs get_uvs(unsigned int texture_size, unsigned int icon_size) const;
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

public:
    explicit GLToolbar(EType type);
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

    void enable_item(const std::string& name);
    void disable_item(const std::string& name);
    void select_item(const std::string& name);

    bool is_item_pressed(const std::string& name) const;
    bool is_item_disabled(const std::string& name) const;
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
    bool is_item_visible(const std::string& name) const;
    void set_item_visible(const std::string& name, bool visible);
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

    std::string update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent);

    // returns the id of the item under the given mouse position or -1 if none
    int contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;

    void do_action(unsigned int item_id, GLCanvas3D& parent);

    void render(const GLCanvas3D& parent) const;    

private:
    void calc_layout() const;
    float get_width_horizontal() const;
    float get_width_vertical() const;
    float get_height_horizontal() const;
    float get_height_vertical() const;
    float get_main_size() const;
    std::string update_hover_state_horizontal(const Vec2d& mouse_pos, GLCanvas3D& parent);
    std::string update_hover_state_vertical(const Vec2d& mouse_pos, GLCanvas3D& parent);
    int contains_mouse_horizontal(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;
    int contains_mouse_vertical(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;

    void render_horizontal(const GLCanvas3D& parent) const;
    void render_vertical(const GLCanvas3D& parent) const;

#if ENABLE_SVG_ICONS
    bool generate_icons_texture() const;
#endif // ENABLE_SVG_ICONS
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLToolbar_hpp_
