#ifndef slic3r_GLToolbar_hpp_
#define slic3r_GLToolbar_hpp_

#include <functional>
#include <string>
#include <vector>

#include <wx/event.h>

#include "../../slic3r/GUI/GLTexture.hpp"
// #include "callback.hpp"

class wxEvtHandler;

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

wxDECLARE_EVENT(EVT_GLTOOLBAR_ADD, wxCommandEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_DELETE, wxCommandEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_TODO_MORE, wxCommandEvent);

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

    // typedef std::function<void()> Callback;
    // typedef PerlCallback Callback;

    struct Data
    {
        std::string name;
        std::string tooltip;
        unsigned int sprite_id;
        bool is_toggable;
        // Callback *action_callback;
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
    bool is_hovered() const;
    bool is_pressed() const;

    bool is_toggable() const;
    bool is_separator() const;

    void render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const;

private:
    GLTexture::Quad_UVs get_uvs(unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const;
};

class GLToolbar
{
public:
    // items icon textures are assumed to be square and all with the same size in pixels, no internal check is done
    // icons are layed-out into the texture starting from the top-left corner in the same order as enum GLToolbarItem::EState
    // from left to right
    struct ItemsIconsTexture
    {
        GLTexture texture;
        // size of the square icons, in pixels
        unsigned int items_icon_size;
        // distance from the border, in pixels
        unsigned int items_icon_border_size;
        // distance between two adjacent icons (to avoid filtering artifacts), in pixels
        unsigned int items_icon_gap_size;

        ItemsIconsTexture();
    };

    struct Layout
    {
        enum Type : unsigned char
        {
            Horizontal,
            Vertical,
            Num_Types
        };

        Type type;
        float top;
        float left;
        float separator_size;
        float gap_size;

        Layout();
    };

private:
    typedef std::vector<GLToolbarItem*> ItemsList;

    GLCanvas3D& m_parent;
    bool m_enabled;
    ItemsIconsTexture m_icons_texture;
    Layout m_layout;

    ItemsList m_items;

public:
    explicit GLToolbar(GLCanvas3D& parent);

    bool init(const std::string& icons_texture_filename, unsigned int items_icon_size, unsigned int items_icon_border_size, unsigned int items_icon_gap_size);
    
    Layout::Type get_layout_type() const;
    void set_layout_type(Layout::Type type);

    void set_position(float top, float left);
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

    bool is_item_pressed(const std::string& name) const;

    void update_hover_state(const Vec2d& mouse_pos);

    // returns the id of the item under the given mouse position or -1 if none
    int contains_mouse(const Vec2d& mouse_pos) const;

    void do_action(unsigned int item_id);

    void render() const;

private:
    float get_width_horizontal() const;
    float get_width_vertical() const;
    float get_height_horizontal() const;
    float get_height_vertical() const;
    float get_main_size() const;
    void update_hover_state_horizontal(const Vec2d& mouse_pos);
    void update_hover_state_vertical(const Vec2d& mouse_pos);
    int contains_mouse_horizontal(const Vec2d& mouse_pos) const;
    int contains_mouse_vertical(const Vec2d& mouse_pos) const;

    void render_horizontal() const;
    void render_vertical() const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLToolbar_hpp_
