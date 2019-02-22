#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

#include "GLToolbar.hpp"

#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/event.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>
#include <wx/glcanvas.h>

namespace Slic3r {
namespace GUI {


wxDEFINE_EVENT(EVT_GLTOOLBAR_ADD, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_DELETE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_DELETE_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_ARRANGE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_MORE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_FEWER, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SPLIT_OBJECTS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SPLIT_VOLUMES, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_LAYERSEDITING, SimpleEvent);

wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_3D, SimpleEvent);
wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_PREVIEW, SimpleEvent);

GLToolbarItem::Data::Data()
    : name("")
    , tooltip("")
    , sprite_id(-1)
    , is_toggable(false)
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
    , visible(true)
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS
{
}

GLToolbarItem::GLToolbarItem(GLToolbarItem::EType type, const GLToolbarItem::Data& data)
    : m_type(type)
    , m_state(Disabled)
    , m_data(data)
{
}

void GLToolbarItem::do_action(wxEvtHandler *target)
{
    wxPostEvent(target, SimpleEvent(m_data.action_event));
}

void GLToolbarItem::render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int texture_size, unsigned int icon_size) const
{
    GLTexture::render_sub_texture(tex_id, left, right, bottom, top, get_uvs(texture_size, icon_size));
}

GLTexture::Quad_UVs GLToolbarItem::get_uvs(unsigned int texture_size, unsigned int icon_size) const
{
    GLTexture::Quad_UVs uvs;

    float inv_texture_size = (texture_size != 0) ? 1.0f / (float)texture_size : 0.0f;

    float scaled_icon_size = (float)icon_size * inv_texture_size;
    float left = (float)m_state * scaled_icon_size;
    float right = left + scaled_icon_size;
    float top = (float)m_data.sprite_id * scaled_icon_size;
    float bottom = top + scaled_icon_size;

    uvs.left_top = { left, top };
    uvs.left_bottom = { left, bottom };
    uvs.right_bottom = { right, bottom };
    uvs.right_top = { right, top };
    
    return uvs;
}

ItemsIconsTexture::Metadata::Metadata()
    : filename("")
    , icon_size(0)
{
}

BackgroundTexture::Metadata::Metadata()
    : filename("")
    , left(0)
    , right(0)
    , top(0)
    , bottom(0)
{
}

GLToolbar::Layout::Layout()
    : type(Horizontal)
    , orientation(Center)
    , top(0.0f)
    , left(0.0f)
    , border(0.0f)
    , separator_size(0.0f)
    , gap_size(0.0f)
    , icons_scale(1.0f)
    , width(0.0f)
    , height(0.0f)
    , dirty(true)
{
}

GLToolbar::GLToolbar(GLToolbar::EType type)
    : m_type(type)
    , m_enabled(false)
{
}

GLToolbar::~GLToolbar()
{
    for (GLToolbarItem* item : m_items)
    {
        delete item;
    }
}

bool GLToolbar::init(const ItemsIconsTexture::Metadata& icons_texture, const BackgroundTexture::Metadata& background_texture)
{
    if (m_icons_texture.texture.get_id() != 0)
        return true;

    std::string path = resources_dir() + "/icons/";
    bool res = !icons_texture.filename.empty() && m_icons_texture.texture.load_from_file(path + icons_texture.filename, false);
    if (res)
        m_icons_texture.metadata = icons_texture;

    if (!background_texture.filename.empty())
        res = m_background_texture.texture.load_from_file(path + background_texture.filename, false);

    if (res)
        m_background_texture.metadata = background_texture;

    return res;
}

GLToolbar::Layout::EType GLToolbar::get_layout_type() const
{
    return m_layout.type;
}

void GLToolbar::set_layout_type(GLToolbar::Layout::EType type)
{
    m_layout.type = type;
    m_layout.dirty = true;
}

GLToolbar::Layout::EOrientation GLToolbar::get_layout_orientation() const
{
    return m_layout.orientation;
}

void GLToolbar::set_layout_orientation(GLToolbar::Layout::EOrientation orientation)
{
    m_layout.orientation = orientation;
}

void GLToolbar::set_position(float top, float left)
{
    m_layout.top = top;
    m_layout.left = left;
}

void GLToolbar::set_border(float border)
{
    m_layout.border = border;
    m_layout.dirty = true;
}

void GLToolbar::set_separator_size(float size)
{
    m_layout.separator_size = size;
    m_layout.dirty = true;
}

void GLToolbar::set_gap_size(float size)
{
    m_layout.gap_size = size;
    m_layout.dirty = true;
}

void GLToolbar::set_icons_scale(float scale)
{
    m_layout.icons_scale = scale;
    m_layout.dirty = true;
}

bool GLToolbar::is_enabled() const
{
    return m_enabled;
}

void GLToolbar::set_enabled(bool enable)
{
    m_enabled = true;
}

bool GLToolbar::add_item(const GLToolbarItem::Data& data)
{
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Action, data);
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    m_layout.dirty = true;
    return true;
}

bool GLToolbar::add_separator()
{
    GLToolbarItem::Data data;
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Separator, data);
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    m_layout.dirty = true;
    return true;
}

float GLToolbar::get_width() const
{
    if (m_layout.dirty)
        calc_layout();

    return m_layout.width;
}

float GLToolbar::get_height() const
{
    if (m_layout.dirty)
        calc_layout();

    return m_layout.height;
}

void GLToolbar::enable_item(const std::string& name)
{
    for (GLToolbarItem* item : m_items)
    {
        if ((item->get_name() == name) && (item->get_state() == GLToolbarItem::Disabled))
        {
            item->set_state(GLToolbarItem::Normal);
            return;
        }
    }
}

void GLToolbar::disable_item(const std::string& name)
{
    for (GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
        {
            item->set_state(GLToolbarItem::Disabled);
            return;
        }
    }
}

void GLToolbar::select_item(const std::string& name)
{
    if (is_item_disabled(name))
        return;

    for (GLToolbarItem* item : m_items)
    {
        if (!item->is_disabled())
        {
            bool hover = item->is_hovered();
            item->set_state((item->get_name() == name) ? (hover ? GLToolbarItem::HoverPressed : GLToolbarItem::Pressed) : (hover ? GLToolbarItem::Hover : GLToolbarItem::Normal));
        }
    }
}

bool GLToolbar::is_item_pressed(const std::string& name) const
{
    for (GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_pressed();
    }

    return false;
}

bool GLToolbar::is_item_disabled(const std::string& name) const
{
    for (GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_disabled();
    }

    return false;
}

#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
bool GLToolbar::is_item_visible(const std::string& name) const
{
    for (GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_visible();
    }

    return false;
}

void GLToolbar::set_item_visible(const std::string& name, bool visible)
{
    for (GLToolbarItem* item : m_items)
    {
        if ((item->get_name() == name) && (item->is_visible() != visible))
        {
            item->set_visible(visible);
            m_layout.dirty = true;
            break;
        }
    }

    // updates separators visibility to avoid having two consecutive
    bool any_item_visible = false;
    for (GLToolbarItem* item : m_items)
    {
        if (!item->is_separator())
            any_item_visible |= item->is_visible();
        else
        {
            item->set_visible(any_item_visible);
            any_item_visible = false;
        }
    }

}
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

std::string GLToolbar::update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    if (!m_enabled)
        return "";

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal: { return update_hover_state_horizontal(mouse_pos, parent); }
    case Layout::Vertical: { return update_hover_state_vertical(mouse_pos, parent); }
    }
}

int GLToolbar::contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    if (!m_enabled)
        return -1;

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal: { return contains_mouse_horizontal(mouse_pos, parent); }
    case Layout::Vertical: { return contains_mouse_vertical(mouse_pos, parent); }
    }
}

void GLToolbar::do_action(unsigned int item_id, GLCanvas3D& parent)
{
    if (item_id < (unsigned int)m_items.size())
    {
        GLToolbarItem* item = m_items[item_id];
        if ((item != nullptr) && !item->is_separator() && item->is_hovered())
        {
            if (item->is_toggable())
            {
                GLToolbarItem::EState state = item->get_state();
                if (state == GLToolbarItem::Hover)
                    item->set_state(GLToolbarItem::HoverPressed);
                else if (state == GLToolbarItem::HoverPressed)
                    item->set_state(GLToolbarItem::Hover);

                parent.render();
                item->do_action(parent.get_wxglcanvas());
            }
            else
            {
                if (m_type == Radio)
                    select_item(item->get_name());
                else
                    item->set_state(GLToolbarItem::HoverPressed);

                parent.render();
                item->do_action(parent.get_wxglcanvas());
                if ((m_type == Normal) && (item->get_state() != GLToolbarItem::Disabled))
                {
                    // the item may get disabled during the action, if not, set it back to hover state
                    item->set_state(GLToolbarItem::Hover);
                    parent.render();
                }
            }
        }
    }
}

void GLToolbar::render(const GLCanvas3D& parent) const
{
    if (!m_enabled || m_items.empty())
        return;

    ::glDisable(GL_DEPTH_TEST);

    ::glPushMatrix();
    ::glLoadIdentity();

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal: { render_horizontal(parent); break; }
    case Layout::Vertical: { render_vertical(parent); break; }
    }

    ::glPopMatrix();
}

void GLToolbar::calc_layout() const
{
    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal:
    {
        m_layout.width = get_width_horizontal();
        m_layout.height = get_height_horizontal();
        break;
    }
    case Layout::Vertical:
    {
        m_layout.width = get_width_vertical();
        m_layout.height = get_height_vertical();
        break;
    }
    }

    m_layout.dirty = false;
}

float GLToolbar::get_width_horizontal() const
{
    return get_main_size();
}

float GLToolbar::get_width_vertical() const
{
    return 2.0f * m_layout.border * m_layout.icons_scale + m_icons_texture.metadata.icon_size * m_layout.icons_scale;
}

float GLToolbar::get_height_horizontal() const
{
    return 2.0f * m_layout.border * m_layout.icons_scale + m_icons_texture.metadata.icon_size * m_layout.icons_scale;
}

float GLToolbar::get_height_vertical() const
{
    return get_main_size();
}

float GLToolbar::get_main_size() const
{
    float size = 2.0f * m_layout.border * m_layout.icons_scale;
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
    {
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
        if (!m_items[i]->is_visible())
            continue;
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

        if (m_items[i]->is_separator())
            size += m_layout.separator_size * m_layout.icons_scale;
        else
            size += (float)m_icons_texture.metadata.icon_size * m_layout.icons_scale;
    }

    if (m_items.size() > 1)
        size += ((float)m_items.size() - 1.0f) * m_layout.gap_size * m_layout.icons_scale;

    return size;
}

std::string GLToolbar::update_hover_state_horizontal(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    // NB: mouse_pos is already scaled appropriately

    float zoom = parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = m_layout.icons_scale * inv_zoom;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left + scaled_border;
    float top = m_layout.top - scaled_border;

    std::string tooltip = "";
        
    for (GLToolbarItem* item : m_items)
    {
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
        if (!item->is_visible())
            continue;
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

        if (item->is_separator())
            left += separator_stride;
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            GLToolbarItem::EState state = item->get_state();
            bool inside = (left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top);
            if (inside)
                tooltip = item->get_tooltip();

            switch (state)
            {
            case GLToolbarItem::Normal:
            {
                if (inside)
                {
                    item->set_state(GLToolbarItem::Hover);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Hover:
            {
                if (!inside)
                {
                    item->set_state(GLToolbarItem::Normal);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Pressed:
            {
                if (inside)
                {
                    item->set_state(GLToolbarItem::HoverPressed);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::HoverPressed:
            {
                if (!inside)
                {
                    item->set_state(GLToolbarItem::Pressed);
                    parent.set_as_dirty();
                }

                break;
            }
            default:
            case GLToolbarItem::Disabled:
            {
                break;
            }
            }

            left += icon_stride;
        }
    }

    return tooltip;
}

std::string GLToolbar::update_hover_state_vertical(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    // NB: mouse_pos is already scaled appropriately

    float zoom = parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = m_layout.icons_scale * inv_zoom;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;
    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left + scaled_border;
    float top = m_layout.top - scaled_border;

    std::string tooltip = "";

    for (GLToolbarItem* item : m_items)
    {
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
        if (!item->is_visible())
            continue;
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

        if (item->is_separator())
            top -= separator_stride;
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            GLToolbarItem::EState state = item->get_state();
            bool inside = (left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top);
            if (inside)
                tooltip = item->get_tooltip();

            switch (state)
            {
            case GLToolbarItem::Normal:
            {
                if (inside)
                {
                    item->set_state(GLToolbarItem::Hover);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Hover:
            {
                if (!inside)
                {
                    item->set_state(GLToolbarItem::Normal);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Pressed:
            {
                if (inside)
                {
                    item->set_state(GLToolbarItem::HoverPressed);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::HoverPressed:
            {
                if (!inside)
                {
                    item->set_state(GLToolbarItem::Pressed);
                    parent.set_as_dirty();
                }

                break;
            }
            default:
            case GLToolbarItem::Disabled:
            {
                break;
            }
            }

            top -= icon_stride;
        }
    }

    return tooltip;
}

int GLToolbar::contains_mouse_horizontal(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    // NB: mouse_pos is already scaled appropriately

    float zoom = parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = m_layout.icons_scale * inv_zoom;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;
    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left + scaled_border;
    float top = m_layout.top - scaled_border;

    int id = -1;
    
    for (GLToolbarItem* item : m_items)
    {
        ++id;
        
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
        if (!item->is_visible())
            continue;
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

        if (item->is_separator())
            left += separator_stride;
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                return id;
            
            left += icon_stride;
        }
    }
    
    return -1;
}

int GLToolbar::contains_mouse_vertical(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    // NB: mouse_pos is already scaled appropriately

    float zoom = parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = m_layout.icons_scale * inv_zoom;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left + scaled_border;
    float top = m_layout.top - scaled_border;

    int id = -1;

    for (GLToolbarItem* item : m_items)
    {
        ++id;

#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
        if (!item->is_visible())
            continue;
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

        if (item->is_separator())
            top -= separator_stride;
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                return id;

            top -= icon_stride;
        }
    }

    return -1;
}

void GLToolbar::render_horizontal(const GLCanvas3D& parent) const
{
    unsigned int tex_id = m_icons_texture.texture.get_id();
    int tex_size = m_icons_texture.texture.get_width();

    if ((tex_id == 0) || (tex_size <= 0))
        return;

    float zoom = parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = inv_zoom * m_layout.icons_scale;

    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;
    float scaled_width = get_width() * inv_zoom;
    float scaled_height = get_height() * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;
    float right = left + scaled_width;
    float bottom = top - scaled_height;

    // renders background
    unsigned int bg_tex_id = m_background_texture.texture.get_id();
    float bg_tex_width = (float)m_background_texture.texture.get_width();
    float bg_tex_height = (float)m_background_texture.texture.get_height();
    if ((bg_tex_id != 0) && (bg_tex_width > 0) && (bg_tex_height > 0))
    {
        float inv_bg_tex_width = (bg_tex_width != 0.0f) ? 1.0f / bg_tex_width : 0.0f;
        float inv_bg_tex_height = (bg_tex_height != 0.0f) ? 1.0f / bg_tex_height : 0.0f;

        float bg_uv_left = 0.0f;
        float bg_uv_right = 1.0f;
        float bg_uv_top = 1.0f;
        float bg_uv_bottom = 0.0f;

        float bg_left = left;
        float bg_right = right;
        float bg_top = top;
        float bg_bottom = bottom;
        float bg_width = right - left;
        float bg_height = top - bottom;
        float bg_min_size = std::min(bg_width, bg_height);

        float bg_uv_i_left = (float)m_background_texture.metadata.left * inv_bg_tex_width;
        float bg_uv_i_right = 1.0f - (float)m_background_texture.metadata.right * inv_bg_tex_width;
        float bg_uv_i_top = 1.0f - (float)m_background_texture.metadata.top * inv_bg_tex_height;
        float bg_uv_i_bottom = (float)m_background_texture.metadata.bottom * inv_bg_tex_height;

        float bg_i_left = bg_left + scaled_border;
        float bg_i_right = bg_right - scaled_border;
        float bg_i_top = bg_top - scaled_border;
        float bg_i_bottom = bg_bottom + scaled_border;

        switch (m_layout.orientation)
        {
        case Layout::Top:
        {
            bg_uv_top = bg_uv_i_top;
            bg_i_top = bg_top;
            break;
        }
        case Layout::Bottom:
        {
            bg_uv_bottom = bg_uv_i_bottom;
            bg_i_bottom = bg_bottom;
            break;
        }
        case Layout::Center:
        {
            break;
        }
        };

        if ((m_layout.border > 0) && (bg_uv_top != bg_uv_i_top))
        {
            if (bg_uv_left != bg_uv_i_left)
                GLTexture::render_sub_texture(bg_tex_id, bg_left, bg_i_left, bg_i_top, bg_top, { { bg_uv_left, bg_uv_i_top }, { bg_uv_i_left, bg_uv_i_top }, { bg_uv_i_left, bg_uv_top }, { bg_uv_left, bg_uv_top } });

            GLTexture::render_sub_texture(bg_tex_id, bg_i_left, bg_i_right, bg_i_top, bg_top, { { bg_uv_i_left, bg_uv_i_top }, { bg_uv_i_right, bg_uv_i_top }, { bg_uv_i_right, bg_uv_top }, { bg_uv_i_left, bg_uv_top } });

            if (bg_uv_right != bg_uv_i_right)
                GLTexture::render_sub_texture(bg_tex_id, bg_i_right, bg_right, bg_i_top, bg_top, { { bg_uv_i_right, bg_uv_i_top }, { bg_uv_right, bg_uv_i_top }, { bg_uv_right, bg_uv_top }, { bg_uv_i_right, bg_uv_top } });
        }

        if ((m_layout.border > 0) && (bg_uv_left != bg_uv_i_left))
            GLTexture::render_sub_texture(bg_tex_id, bg_left, bg_i_left, bg_i_bottom, bg_i_top, { { bg_uv_left, bg_uv_i_bottom }, { bg_uv_i_left, bg_uv_i_bottom }, { bg_uv_i_left, bg_uv_i_top }, { bg_uv_left, bg_uv_i_top } });

        GLTexture::render_sub_texture(bg_tex_id, bg_i_left, bg_i_right, bg_i_bottom, bg_i_top, { { bg_uv_i_left, bg_uv_i_bottom }, { bg_uv_i_right, bg_uv_i_bottom }, { bg_uv_i_right, bg_uv_i_top }, { bg_uv_i_left, bg_uv_i_top } });

        if ((m_layout.border > 0) && (bg_uv_right != bg_uv_i_right))
            GLTexture::render_sub_texture(bg_tex_id, bg_i_right, bg_right, bg_i_bottom, bg_i_top, { { bg_uv_i_right, bg_uv_i_bottom }, { bg_uv_right, bg_uv_i_bottom }, { bg_uv_right, bg_uv_i_top }, { bg_uv_i_right, bg_uv_i_top } });

        if ((m_layout.border > 0) && (bg_uv_bottom != bg_uv_i_bottom))
        {
            if (bg_uv_left != bg_uv_i_left)
                GLTexture::render_sub_texture(bg_tex_id, bg_left, bg_i_left, bg_bottom, bg_i_bottom, { { bg_uv_left, bg_uv_bottom }, { bg_uv_i_left, bg_uv_bottom }, { bg_uv_i_left, bg_uv_i_bottom }, { bg_uv_left, bg_uv_i_bottom } });

            GLTexture::render_sub_texture(bg_tex_id, bg_i_left, bg_i_right, bg_bottom, bg_i_bottom, { { bg_uv_i_left, bg_uv_bottom }, { bg_uv_i_right, bg_uv_bottom }, { bg_uv_i_right, bg_uv_i_bottom }, { bg_uv_i_left, bg_uv_i_bottom } });

            if (bg_uv_right != bg_uv_i_right)
                GLTexture::render_sub_texture(bg_tex_id, bg_i_right, bg_right, bg_bottom, bg_i_bottom, { { bg_uv_i_right, bg_uv_bottom }, { bg_uv_right, bg_uv_bottom }, { bg_uv_right, bg_uv_i_bottom }, { bg_uv_i_right, bg_uv_i_bottom } });
        }
    }

    left += scaled_border;
    top -= scaled_border;

    // renders icons
    for (const GLToolbarItem* item : m_items)
    {
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
        if (!item->is_visible())
            continue;
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

        if (item->is_separator())
            left += separator_stride;
        else
        {
            item->render(tex_id, left, left + scaled_icons_size, top - scaled_icons_size, top, (unsigned int)tex_size, m_icons_texture.metadata.icon_size);
            left += icon_stride;
        }
    }
}

void GLToolbar::render_vertical(const GLCanvas3D& parent) const
{
    unsigned int tex_id = m_icons_texture.texture.get_id();
    int tex_size = m_icons_texture.texture.get_width();

    if ((tex_id == 0) || (tex_size <= 0))
        return;

    float zoom = parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = inv_zoom * m_layout.icons_scale;

    float scaled_icons_size = (float)m_icons_texture.metadata.icon_size * m_layout.icons_scale * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;
    float scaled_width = get_width() * inv_zoom;
    float scaled_height = get_height() * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;
    float right = left + scaled_width;
    float bottom = top - scaled_height;

    // renders background
    unsigned int bg_tex_id = m_background_texture.texture.get_id();
    float bg_tex_width = (float)m_background_texture.texture.get_width();
    float bg_tex_height = (float)m_background_texture.texture.get_height();
    if ((bg_tex_id != 0) && (bg_tex_width > 0) && (bg_tex_height > 0))
    {
        float inv_bg_tex_width = (bg_tex_width != 0.0f) ? 1.0f / bg_tex_width : 0.0f;
        float inv_bg_tex_height = (bg_tex_height != 0.0f) ? 1.0f / bg_tex_height : 0.0f;

        float bg_uv_left = 0.0f;
        float bg_uv_right = 1.0f;
        float bg_uv_top = 1.0f;
        float bg_uv_bottom = 0.0f;

        float bg_left = left;
        float bg_right = right;
        float bg_top = top;
        float bg_bottom = bottom;
        float bg_width = right - left;
        float bg_height = top - bottom;
        float bg_min_size = std::min(bg_width, bg_height);

        float bg_uv_i_left = (float)m_background_texture.metadata.left * inv_bg_tex_width;
        float bg_uv_i_right = 1.0f - (float)m_background_texture.metadata.right * inv_bg_tex_width;
        float bg_uv_i_top = 1.0f - (float)m_background_texture.metadata.top * inv_bg_tex_height;
        float bg_uv_i_bottom = (float)m_background_texture.metadata.bottom * inv_bg_tex_height;

        float bg_i_left = bg_left + scaled_border;
        float bg_i_right = bg_right - scaled_border;
        float bg_i_top = bg_top - scaled_border;
        float bg_i_bottom = bg_bottom + scaled_border;

        switch (m_layout.orientation)
        {
        case Layout::Left:
        {
            bg_uv_left = bg_uv_i_left;
            bg_i_left = bg_left;
            break;
        }
        case Layout::Right:
        {
            bg_uv_right = bg_uv_i_right;
            bg_i_right = bg_right;
            break;
        }
        case Layout::Center:
        {
            break;
        }
        };

        if ((m_layout.border > 0) && (bg_uv_top != bg_uv_i_top))
        {
            if (bg_uv_left != bg_uv_i_left)
                GLTexture::render_sub_texture(bg_tex_id, bg_left, bg_i_left, bg_i_top, bg_top, { { bg_uv_left, bg_uv_i_top }, { bg_uv_i_left, bg_uv_i_top }, { bg_uv_i_left, bg_uv_top }, { bg_uv_left, bg_uv_top } });

            GLTexture::render_sub_texture(bg_tex_id, bg_i_left, bg_i_right, bg_i_top, bg_top, { { bg_uv_i_left, bg_uv_i_top }, { bg_uv_i_right, bg_uv_i_top }, { bg_uv_i_right, bg_uv_top }, { bg_uv_i_left, bg_uv_top } });

            if (bg_uv_right != bg_uv_i_right)
                GLTexture::render_sub_texture(bg_tex_id, bg_i_right, bg_right, bg_i_top, bg_top, { { bg_uv_i_right, bg_uv_i_top }, { bg_uv_right, bg_uv_i_top }, { bg_uv_right, bg_uv_top }, { bg_uv_i_right, bg_uv_top } });
        }

        if ((m_layout.border > 0) && (bg_uv_left != bg_uv_i_left))
            GLTexture::render_sub_texture(bg_tex_id, bg_left, bg_i_left, bg_i_bottom, bg_i_top, { { bg_uv_left, bg_uv_i_bottom }, { bg_uv_i_left, bg_uv_i_bottom }, { bg_uv_i_left, bg_uv_i_top }, { bg_uv_left, bg_uv_i_top } });

        GLTexture::render_sub_texture(bg_tex_id, bg_i_left, bg_i_right, bg_i_bottom, bg_i_top, { { bg_uv_i_left, bg_uv_i_bottom }, { bg_uv_i_right, bg_uv_i_bottom }, { bg_uv_i_right, bg_uv_i_top }, { bg_uv_i_left, bg_uv_i_top } });

        if ((m_layout.border > 0) && (bg_uv_right != bg_uv_i_right))
            GLTexture::render_sub_texture(bg_tex_id, bg_i_right, bg_right, bg_i_bottom, bg_i_top, { { bg_uv_i_right, bg_uv_i_bottom }, { bg_uv_right, bg_uv_i_bottom }, { bg_uv_right, bg_uv_i_top }, { bg_uv_i_right, bg_uv_i_top } });

        if ((m_layout.border > 0) && (bg_uv_bottom != bg_uv_i_bottom))
        {
            if (bg_uv_left != bg_uv_i_left)
                GLTexture::render_sub_texture(bg_tex_id, bg_left, bg_i_left, bg_bottom, bg_i_bottom, { { bg_uv_left, bg_uv_bottom }, { bg_uv_i_left, bg_uv_bottom }, { bg_uv_i_left, bg_uv_i_bottom }, { bg_uv_left, bg_uv_i_bottom } });

            GLTexture::render_sub_texture(bg_tex_id, bg_i_left, bg_i_right, bg_bottom, bg_i_bottom, { { bg_uv_i_left, bg_uv_bottom }, { bg_uv_i_right, bg_uv_bottom }, { bg_uv_i_right, bg_uv_i_bottom }, { bg_uv_i_left, bg_uv_i_bottom } });

            if (bg_uv_right != bg_uv_i_right)
                GLTexture::render_sub_texture(bg_tex_id, bg_i_right, bg_right, bg_bottom, bg_i_bottom, { { bg_uv_i_right, bg_uv_bottom }, { bg_uv_right, bg_uv_bottom }, { bg_uv_right, bg_uv_i_bottom }, { bg_uv_i_right, bg_uv_i_bottom } });
        }
    }

    left += scaled_border;
    top -= scaled_border;

    // renders icons
    for (const GLToolbarItem* item : m_items)
    {
#if ENABLE_MODE_AWARE_TOOLBAR_ITEMS
        if (!item->is_visible())
            continue;
#endif // ENABLE_MODE_AWARE_TOOLBAR_ITEMS

        if (item->is_separator())
            top -= separator_stride;
        else
        {
            item->render(tex_id, left, left + scaled_icons_size, top - scaled_icons_size, top, (unsigned int)tex_size, m_icons_texture.metadata.icon_size);
            top -= icon_stride;
        }
    }
}

} // namespace GUI
} // namespace Slic3r
