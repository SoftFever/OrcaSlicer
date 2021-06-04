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
    typedef std::function<bool()> EnablingCallback;
    typedef std::function<void(float, float, float, float)> RenderCallback;

    enum EType : unsigned char
    {
        Action,
        Separator,
        Num_Types
    };

    enum EActionType : unsigned char
    {
        Undefined,
        Left,
        Right,
        Num_Action_Types
    };

    enum EState : unsigned char
    {
        Normal,
        Pressed,
        Disabled,
        Hover,
        HoverPressed,
        HoverDisabled,
        Num_States
    };

    struct Data
    {
        struct Option
        {
            bool toggable;
            ActionCallback action_callback;
            RenderCallback render_callback;

            Option();

            bool can_render() const { return toggable && (render_callback != nullptr); }
        };

        std::string name;
        std::string icon_filename;
        std::string tooltip;
        std::string additional_tooltip;
        unsigned int sprite_id;
        // mouse left click
        Option left;
        // mouse right click
        Option right;
        bool visible;
        VisibilityCallback visibility_callback;
        EnablingCallback enabling_callback;

        Data();
    };

    static const ActionCallback Default_Action_Callback;
    static const VisibilityCallback Default_Visibility_Callback;
    static const EnablingCallback Default_Enabling_Callback;
    static const RenderCallback Default_Render_Callback;

private:
    EType m_type;
    EState m_state;
    Data m_data;
    EActionType m_last_action_type;

public:
    GLToolbarItem(EType type, const Data& data);

    EState get_state() const { return m_state; }
    void set_state(EState state) { m_state = state; }

    const std::string& get_name() const { return m_data.name; }
    const std::string& get_icon_filename() const { return m_data.icon_filename; }
    const std::string& get_tooltip() const { return m_data.tooltip; }
    const std::string& get_additional_tooltip() const { return m_data.additional_tooltip; }
    void set_additional_tooltip(const std::string& text) { m_data.additional_tooltip = text; }
    void set_tooltip(const std::string& text)            { m_data.tooltip = text; }

    void do_left_action() { m_last_action_type = Left; m_data.left.action_callback(); }
    void do_right_action() { m_last_action_type = Right; m_data.right.action_callback(); }

    bool is_enabled() const { return (m_state != Disabled) && (m_state != HoverDisabled); }
    bool is_disabled() const { return (m_state == Disabled) || (m_state == HoverDisabled); }
    bool is_hovered() const { return (m_state == Hover) || (m_state == HoverPressed) || (m_state == HoverDisabled); }
    bool is_pressed() const { return (m_state == Pressed) || (m_state == HoverPressed); }
    bool is_visible() const { return m_data.visible; }
    bool is_separator() const { return m_type == Separator; }

    bool is_left_toggable() const { return m_data.left.toggable; }
    bool is_right_toggable() const { return m_data.right.toggable; }

    bool has_left_render_callback() const { return m_data.left.render_callback != nullptr; }
    bool has_right_render_callback() const { return m_data.right.render_callback != nullptr; }

    EActionType get_last_action_type() const { return m_last_action_type; }
    void reset_last_action_type() { m_last_action_type = Undefined; }

    // returns true if the state changes
    bool update_visibility();
    // returns true if the state changes
    bool update_enabled_state();

    void render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int tex_width, unsigned int tex_height, unsigned int icon_size) const;

private:
    void set_visible(bool visible) { m_data.visible = visible; }

    friend class GLToolbar;
};

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
    static const float Default_Icons_Size;

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

        enum EHorizontalOrientation : unsigned char
        {
            HO_Left,
            HO_Center,
            HO_Right,
            Num_Horizontal_Orientations
        };

        enum EVerticalOrientation : unsigned char
        {
            VO_Top,
            VO_Center,
            VO_Bottom,
            Num_Vertical_Orientations
        };

        EType type;
        EHorizontalOrientation horizontal_orientation;
        EVerticalOrientation vertical_orientation;
        float top;
        float left;
        float border;
        float separator_size;
        float gap_size;
        float icons_size;
        float scale;

        float width;
        float height;
        bool dirty;

        Layout();
    };

private:
    typedef std::vector<GLToolbarItem*> ItemsList;

    EType m_type;
    std::string m_name;
    bool m_enabled;
    GLTexture m_icons_texture;
    bool m_icons_texture_dirty;
    BackgroundTexture m_background_texture;
    Layout m_layout;

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
    int m_pressed_toggable_id;

public:
    GLToolbar(EType type, const std::string& name);
    ~GLToolbar();

    bool init(const BackgroundTexture::Metadata& background_texture);

    Layout::EType get_layout_type() const;
    void set_layout_type(Layout::EType type);
    Layout::EHorizontalOrientation get_horizontal_orientation() const { return m_layout.horizontal_orientation; }
    void set_horizontal_orientation(Layout::EHorizontalOrientation orientation) { m_layout.horizontal_orientation = orientation; }
    Layout::EVerticalOrientation get_vertical_orientation() const { return m_layout.vertical_orientation; }
    void set_vertical_orientation(Layout::EVerticalOrientation orientation) { m_layout.vertical_orientation = orientation; }

    void set_position(float top, float left);
    void set_border(float border);
    void set_separator_size(float size);
    void set_gap_size(float size);
    void set_icons_size(float size);
    void set_scale(float scale);

    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool enable) { m_enabled = enable; }

    bool add_item(const GLToolbarItem::Data& data);
    bool add_separator();

    float get_width();
    float get_height();

    void select_item(const std::string& name);

    bool is_item_pressed(const std::string& name) const;
    bool is_item_disabled(const std::string& name) const;
    bool is_item_visible(const std::string& name) const;

    bool is_any_item_pressed() const;

    unsigned int get_items_count() const { return (unsigned int)m_items.size(); }
    int get_item_id(const std::string& name) const;

    void force_left_action(int item_id, GLCanvas3D& parent) { do_action(GLToolbarItem::Left, item_id, parent, false); }
    void force_right_action(int item_id, GLCanvas3D& parent) { do_action(GLToolbarItem::Right, item_id, parent, false); }

    std::string get_tooltip() const;

    void get_additional_tooltip(int item_id, std::string& text);
    void set_additional_tooltip(int item_id, const std::string& text);
    void set_tooltip(int item_id, const std::string& text);
    int  get_visible_items_cnt() const;

    // returns true if any item changed its state
    bool update_items_state();

    void render(const GLCanvas3D& parent);

    bool on_mouse(wxMouseEvent& evt, GLCanvas3D& parent);

private:
    void calc_layout();
    float get_width_horizontal() const;
    float get_width_vertical() const;
    float get_height_horizontal() const;
    float get_height_vertical() const;
    float get_main_size() const;
    void do_action(GLToolbarItem::EActionType type, int item_id, GLCanvas3D& parent, bool check_hover);
    void update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent);
    void update_hover_state_horizontal(const Vec2d& mouse_pos, GLCanvas3D& parent);
    void update_hover_state_vertical(const Vec2d& mouse_pos, GLCanvas3D& parent);
    // returns the id of the item under the given mouse position or -1 if none
    int contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;
    int contains_mouse_horizontal(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;
    int contains_mouse_vertical(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;

    void render_background(float left, float top, float right, float bottom, float border) const;
    void render_horizontal(const GLCanvas3D& parent);
    void render_vertical(const GLCanvas3D& parent);

    bool generate_icons_texture();

    // returns true if any item changed its state
    bool update_items_visibility();
    // returns true if any item changed its state
    bool update_items_enabled_state();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLToolbar_hpp_
