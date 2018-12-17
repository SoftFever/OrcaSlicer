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
        std::string tooltip;
        unsigned int sprite_id;
        bool is_toggable;
        wxEventType action_event;

        Data();
    };

private:
    EType m_type;
    EState m_state;
    Data m_data;

public:
    GLToolbarItem(EType type, const Data& data);

    EState get_state() const;
    void set_state(EState state);

    const std::string& get_name() const;
    const std::string& get_tooltip() const;

    void do_action(wxEvtHandler *target);

    bool is_enabled() const;
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    bool is_disabled() const;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    bool is_hovered() const;
    bool is_pressed() const;

    bool is_toggable() const;
    bool is_separator() const;

    void render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const;

private:
    GLTexture::Quad_UVs get_uvs(unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const;
};

// items icon textures are assumed to be square and all with the same size in pixels, no internal check is done
// icons are layed-out into the texture starting from the top-left corner in the same order as enum GLToolbarItem::EState
// from left to right
struct ItemsIconsTexture
{
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    struct Metadata
    {
        // path of the file containing the icons' texture
        std::string filename;
        // size of the square icons, in pixels
        unsigned int icon_size;
        // size of the border, in pixels
        unsigned int icon_border_size;
        // distance between two adjacent icons (to avoid filtering artifacts), in pixels
        unsigned int icon_gap_size;

        Metadata();
    };
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    GLTexture texture;
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    Metadata metadata;
#else
    // size of the square icons, in pixels
    unsigned int items_icon_size;
    // distance from the border, in pixels
    unsigned int items_icon_border_size;
    // distance between two adjacent icons (to avoid filtering artifacts), in pixels
    unsigned int items_icon_gap_size;

    ItemsIconsTexture();
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
};

#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
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
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

class GLToolbar
{
public:
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    enum EType : unsigned char
    {
        Normal,
        Radio,
        Num_Types
    };
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

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
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
        EOrientation orientation;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
        float top;
        float left;
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
        float border;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
        float separator_size;
        float gap_size;

#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
        float width;
        float height;
        bool dirty;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

        Layout();
    };

private:
    typedef std::vector<GLToolbarItem*> ItemsList;

#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    EType m_type;
#else
    GLCanvas3D& m_parent;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    bool m_enabled;
    ItemsIconsTexture m_icons_texture;
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    BackgroundTexture m_background_texture;
    mutable Layout m_layout;
#else
    Layout m_layout;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

    ItemsList m_items;

public:
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    explicit GLToolbar(EType type);
#else
    explicit GLToolbar(GLCanvas3D& parent);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    ~GLToolbar();

#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    bool init(const ItemsIconsTexture::Metadata& icons_texture, const BackgroundTexture::Metadata& background_texture);
#else
    bool init(const std::string& icons_texture_filename, unsigned int items_icon_size, unsigned int items_icon_border_size, unsigned int items_icon_gap_size);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

    Layout::EType get_layout_type() const;
    void set_layout_type(Layout::EType type);
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    Layout::EOrientation get_layout_orientation() const;
    void set_layout_orientation(Layout::EOrientation orientation);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

    void set_position(float top, float left);
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    void set_border(float border);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    void set_separator_size(float size);
    void set_gap_size(float size);

    bool is_enabled() const;
    void set_enabled(bool enable);

    bool add_item(const GLToolbarItem::Data& data);
    bool add_separator();

    float get_width() const;
    float get_height() const;

    void enable_item(const std::string& name);
    void disable_item(const std::string& name);
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    void select_item(const std::string& name);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

    bool is_item_pressed(const std::string& name) const;
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    bool is_item_disabled(const std::string& name) const;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

#if ENABLE_REMOVE_TABS_FROM_PLATER
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    std::string update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent);
#else
    std::string update_hover_state(const Vec2d& mouse_pos);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
#else
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    void update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent);
#else
    void update_hover_state(const Vec2d& mouse_pos);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
#endif // ENABLE_REMOVE_TABS_FROM_PLATER

#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    // returns the id of the item under the given mouse position or -1 if none
    int contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;

    void do_action(unsigned int item_id, GLCanvas3D& parent);
#else
    // returns the id of the item under the given mouse position or -1 if none
    int contains_mouse(const Vec2d& mouse_pos) const;

    void do_action(unsigned int item_id);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    void render(const GLCanvas3D& parent) const;    
#else
    void render() const;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE

private:
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    void calc_layout() const;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    float get_width_horizontal() const;
    float get_width_vertical() const;
    float get_height_horizontal() const;
    float get_height_vertical() const;
    float get_main_size() const;
#if ENABLE_REMOVE_TABS_FROM_PLATER
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    std::string update_hover_state_horizontal(const Vec2d& mouse_pos, GLCanvas3D& parent);
    std::string update_hover_state_vertical(const Vec2d& mouse_pos, GLCanvas3D& parent);
#else
    std::string update_hover_state_horizontal(const Vec2d& mouse_pos);
    std::string update_hover_state_vertical(const Vec2d& mouse_pos);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
#else
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    void update_hover_state_horizontal(const Vec2d& mouse_pos, GLCanvas3D& parent);
    void update_hover_state_vertical(const Vec2d& mouse_pos, GLCanvas3D& parent);
#else
    void update_hover_state_horizontal(const Vec2d& mouse_pos);
    void update_hover_state_vertical(const Vec2d& mouse_pos);
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
#endif // ENABLE_REMOVE_TABS_FROM_PLATER
#if ENABLE_TOOLBAR_BACKGROUND_TEXTURE
    int contains_mouse_horizontal(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;
    int contains_mouse_vertical(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;

    void render_horizontal(const GLCanvas3D& parent) const;
    void render_vertical(const GLCanvas3D& parent) const;
#else
    int contains_mouse_horizontal(const Vec2d& mouse_pos) const;
    int contains_mouse_vertical(const Vec2d& mouse_pos) const;

    void render_horizontal() const;
    void render_vertical() const;
#endif // ENABLE_TOOLBAR_BACKGROUND_TEXTURE
};

#if !ENABLE_TOOLBAR_BACKGROUND_TEXTURE
class GLRadioToolbarItem
{
public:
    struct Data
    {
        std::string name;
        std::string tooltip;
        unsigned int sprite_id;
        wxEventType action_event;

        Data();
    };

    enum EState : unsigned char
    {
        Normal,
        Pressed,
        Hover,
        HoverPressed,
        Num_States
    };

private:
    EState m_state;
    Data m_data;

public:
    GLRadioToolbarItem(const Data& data);

    EState get_state() const;
    void set_state(EState state);

    const std::string& get_name() const;
    const std::string& get_tooltip() const;

    bool is_hovered() const;
    bool is_pressed() const;

    void do_action(wxEvtHandler *target);

    void render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const;

private:
    GLTexture::Quad_UVs get_uvs(unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const;
};

class GLRadioToolbar
{
    typedef std::vector<GLRadioToolbarItem*> ItemsList;

    ItemsIconsTexture m_icons_texture;

    ItemsList m_items;
    float m_top;
    float m_left;

public:
    GLRadioToolbar();
    ~GLRadioToolbar();

    bool init(const std::string& icons_texture_filename, unsigned int items_icon_size, unsigned int items_icon_border_size, unsigned int items_icon_gap_size);

    bool add_item(const GLRadioToolbarItem::Data& data);

    float get_height() const;

    void set_position(float top, float left);
    void set_selection(const std::string& name);

    // returns the id of the item under the given mouse position or -1 if none
    int contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;

    std::string update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent);

    void do_action(unsigned int item_id, GLCanvas3D& parent);

    void render(const GLCanvas3D& parent) const;
};
#endif // !ENABLE_TOOLBAR_BACKGROUND_TEXTURE

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLToolbar_hpp_
