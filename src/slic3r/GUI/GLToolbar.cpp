#include "../../libslic3r/Point.hpp"
#include "GLToolbar.hpp"

#include "../../libslic3r/libslic3r.h"
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
{
}

GLToolbarItem::GLToolbarItem(GLToolbarItem::EType type, const GLToolbarItem::Data& data)
    : m_type(type)
    , m_state(Disabled)
    , m_data(data)
{
}

GLToolbarItem::EState GLToolbarItem::get_state() const
{
    return m_state;
}

void GLToolbarItem::set_state(GLToolbarItem::EState state)
{
    m_state = state;
}

const std::string& GLToolbarItem::get_name() const
{
    return m_data.name;
}

const std::string& GLToolbarItem::get_tooltip() const
{
    return m_data.tooltip;
}

void GLToolbarItem::do_action(wxEvtHandler *target)
{
    wxPostEvent(target, SimpleEvent(m_data.action_event));
}

bool GLToolbarItem::is_enabled() const
{
    return m_state != Disabled;
}

bool GLToolbarItem::is_hovered() const
{
    return (m_state == Hover) || (m_state == HoverPressed);
}

bool GLToolbarItem::is_pressed() const
{
    return (m_state == Pressed) || (m_state == HoverPressed);
}

bool GLToolbarItem::is_toggable() const
{
    return m_data.is_toggable;
}

bool GLToolbarItem::is_separator() const
{
    return m_type == Separator;
}

void GLToolbarItem::render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const
{
    GLTexture::render_sub_texture(tex_id, left, right, bottom, top, get_uvs(texture_size, border_size, icon_size, gap_size));
}

GLTexture::Quad_UVs GLToolbarItem::get_uvs(unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const
{
    GLTexture::Quad_UVs uvs;

    float inv_texture_size = (texture_size != 0) ? 1.0f / (float)texture_size : 0.0f;

    float scaled_icon_size = (float)icon_size * inv_texture_size;
    float scaled_border_size = (float)border_size * inv_texture_size;
    float scaled_gap_size = (float)gap_size * inv_texture_size;
    float stride = scaled_icon_size + scaled_gap_size;

    float left = scaled_border_size + (float)m_state * stride;
    float right = left + scaled_icon_size;
    float top = scaled_border_size + (float)m_data.sprite_id * stride;
    float bottom = top + scaled_icon_size;

    uvs.left_top = { left, top };
    uvs.left_bottom = { left, bottom };
    uvs.right_bottom = { right, bottom };
    uvs.right_top = { right, top };
    
    return uvs;
}

ItemsIconsTexture::ItemsIconsTexture()
    : items_icon_size(0)
    , items_icon_border_size(0)
    , items_icon_gap_size(0)
{
}

GLToolbar::Layout::Layout()
    : type(Horizontal)
    , top(0.0f)
    , left(0.0f)
    , separator_size(0.0f)
    , gap_size(0.0f)
{
}

GLToolbar::GLToolbar(GLCanvas3D& parent)
    : m_parent(parent)
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

bool GLToolbar::init(const std::string& icons_texture_filename, unsigned int items_icon_size, unsigned int items_icon_border_size, unsigned int items_icon_gap_size)
{
    std::string path = resources_dir() + "/icons/";
    bool res = !icons_texture_filename.empty() && m_icons_texture.texture.load_from_file(path + icons_texture_filename, false);
    if (res)
    {
        m_icons_texture.items_icon_size = items_icon_size;
        m_icons_texture.items_icon_border_size = items_icon_border_size;
        m_icons_texture.items_icon_gap_size = items_icon_gap_size;
    }

    return res;
}

GLToolbar::Layout::Type GLToolbar::get_layout_type() const
{
    return m_layout.type;
}

void GLToolbar::set_layout_type(GLToolbar::Layout::Type type)
{
    m_layout.type = type;
}

void GLToolbar::set_position(float top, float left)
{
    m_layout.top = top;
    m_layout.left = left;
}

void GLToolbar::set_separator_size(float size)
{
    m_layout.separator_size = size;
}

void GLToolbar::set_gap_size(float size)
{
    m_layout.gap_size = size;
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
    return true;
}

bool GLToolbar::add_separator()
{
    GLToolbarItem::Data data;
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Separator, data);
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    return true;
}

float GLToolbar::get_width() const
{
    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal:
    {
        return get_width_horizontal();
    }
    case Layout::Vertical:
    {
        return get_width_vertical();
    }
    }
}

float GLToolbar::get_height() const
{
    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal:
    {
        return get_height_horizontal();
    }
    case Layout::Vertical:
    {
        return get_height_vertical();
    }
    }
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

bool GLToolbar::is_item_pressed(const std::string& name) const
{
    for (GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_pressed();
    }

    return false;
}

#if ENABLE_REMOVE_TABS_FROM_PLATER
std::string GLToolbar::update_hover_state(const Vec2d& mouse_pos)
#else
void GLToolbar::update_hover_state(const Vec2d& mouse_pos)
#endif // ENABLE_REMOVE_TABS_FROM_PLATER
{
#if ENABLE_REMOVE_TABS_FROM_PLATER
    if (!m_enabled)
        return "";
#else
    if (!m_enabled)
        return;
#endif // ENABLE_REMOVE_TABS_FROM_PLATER

    switch (m_layout.type)
    {
    default:
#if ENABLE_REMOVE_TABS_FROM_PLATER
    case Layout::Horizontal: { return update_hover_state_horizontal(mouse_pos); }
    case Layout::Vertical: { return update_hover_state_vertical(mouse_pos); }
#else
    case Layout::Horizontal:
    {
        update_hover_state_horizontal(mouse_pos);
        break;
    }
    case Layout::Vertical:
    {
        update_hover_state_vertical(mouse_pos);
        break;
    }
#endif // ENABLE_REMOVE_TABS_FROM_PLATER
    }
}

int GLToolbar::contains_mouse(const Vec2d& mouse_pos) const
{
    if (!m_enabled)
        return -1;

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal:
    {
        return contains_mouse_horizontal(mouse_pos);
    }
    case Layout::Vertical:
    {
        return contains_mouse_vertical(mouse_pos);
    }
    }
}

void GLToolbar::do_action(unsigned int item_id)
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

                m_parent.render();
                item->do_action(m_parent.get_wxglcanvas());
            }
            else
            {
                item->set_state(GLToolbarItem::HoverPressed);
                m_parent.render();
                item->do_action(m_parent.get_wxglcanvas());
                if (item->get_state() != GLToolbarItem::Disabled)
                {
                    // the item may get disabled during the action, if not, set it back to hover state
                    item->set_state(GLToolbarItem::Hover);
                    m_parent.render();
                }
            }
        }
    }
}

void GLToolbar::render() const
{
    if (!m_enabled || m_items.empty())
        return;

    ::glDisable(GL_DEPTH_TEST);

    ::glPushMatrix();
    ::glLoadIdentity();

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal:
    {
        render_horizontal();
        break;
    }
    case Layout::Vertical:
    {
        render_vertical();
        break;
    }
    }

    ::glPopMatrix();
}

float GLToolbar::get_width_horizontal() const
{
    return get_main_size();
}

float GLToolbar::get_width_vertical() const
{
    return m_icons_texture.items_icon_size;
}

float GLToolbar::get_height_horizontal() const
{
    return m_icons_texture.items_icon_size;
}

float GLToolbar::get_height_vertical() const
{
    return get_main_size();
}

float GLToolbar::get_main_size() const
{
    float size = 0.0f;
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
    {
        if (m_items[i]->is_separator())
            size += m_layout.separator_size;
        else
            size += (float)m_icons_texture.items_icon_size;
    }

    if (m_items.size() > 1)
        size += ((float)m_items.size() - 1.0f) * m_layout.gap_size;

    return size;
}

#if ENABLE_REMOVE_TABS_FROM_PLATER
std::string GLToolbar::update_hover_state_horizontal(const Vec2d& mouse_pos)
#else
void GLToolbar::update_hover_state_horizontal(const Vec2d& mouse_pos)
#endif // ENABLE_REMOVE_TABS_FROM_PLATER
{
    float zoom = m_parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size cnv_size = m_parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.items_icon_size * inv_zoom;
    float scaled_separator_size = m_layout.separator_size * inv_zoom;
    float scaled_gap_size = m_layout.gap_size * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;

    std::string tooltip = "";
        
    for (GLToolbarItem* item : m_items)
    {
        if (item->is_separator())
            left += separator_stride;
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            GLToolbarItem::EState state = item->get_state();
            bool inside = (left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top);

            switch (state)
            {
            case GLToolbarItem::Normal:
            {
                if (inside)
                    item->set_state(GLToolbarItem::Hover);

                break;
            }
            case GLToolbarItem::Hover:
            {
                if (inside)
                    tooltip = item->get_tooltip();
                else
                    item->set_state(GLToolbarItem::Normal);
                
                break;
            }
            case GLToolbarItem::Pressed:
            {
                if (inside)
                    item->set_state(GLToolbarItem::HoverPressed);
                
                break;
            }
            case GLToolbarItem::HoverPressed:
            {
                if (inside)
                    tooltip = item->get_tooltip();
                else
                    item->set_state(GLToolbarItem::Pressed);
                
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

#if ENABLE_REMOVE_TABS_FROM_PLATER
    return tooltip;
#else
    if (!tooltip.empty())
        m_parent.set_tooltip(tooltip);
#endif // ENABLE_REMOVE_TABS_FROM_PLATER
}

#if ENABLE_REMOVE_TABS_FROM_PLATER
std::string GLToolbar::update_hover_state_vertical(const Vec2d& mouse_pos)
#else
void GLToolbar::update_hover_state_vertical(const Vec2d& mouse_pos)
#endif // ENABLE_REMOVE_TABS_FROM_PLATER
{
    float zoom = m_parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size cnv_size = m_parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.items_icon_size * inv_zoom;
    float scaled_separator_size = m_layout.separator_size * inv_zoom;
    float scaled_gap_size = m_layout.gap_size * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;

    std::string tooltip = "";

    for (GLToolbarItem* item : m_items)
    {
        if (item->is_separator())
            top -= separator_stride;
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            GLToolbarItem::EState state = item->get_state();
            bool inside = (left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top);

            switch (state)
            {
            case GLToolbarItem::Normal:
            {
                if (inside)
                    item->set_state(GLToolbarItem::Hover);

                break;
            }
            case GLToolbarItem::Hover:
            {
                if (inside)
                    tooltip = item->get_tooltip();
                else
                    item->set_state(GLToolbarItem::Normal);

                break;
            }
            case GLToolbarItem::Pressed:
            {
                if (inside)
                    item->set_state(GLToolbarItem::HoverPressed);

                break;
            }
            case GLToolbarItem::HoverPressed:
            {
                if (inside)
                    tooltip = item->get_tooltip();
                else
                    item->set_state(GLToolbarItem::Pressed);

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

#if ENABLE_REMOVE_TABS_FROM_PLATER
    return tooltip;
#else
    m_parent.set_tooltip(tooltip);
#endif // ENABLE_REMOVE_TABS_FROM_PLATER
}

int GLToolbar::contains_mouse_horizontal(const Vec2d& mouse_pos) const
{
    float zoom = m_parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size cnv_size = m_parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.items_icon_size * inv_zoom;
    float scaled_separator_size = m_layout.separator_size * inv_zoom;
    float scaled_gap_size = m_layout.gap_size * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;

    int id = -1;
    
    for (GLToolbarItem* item : m_items)
    {
        ++id;
        
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

int GLToolbar::contains_mouse_vertical(const Vec2d& mouse_pos) const
{
    float zoom = m_parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size cnv_size = m_parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.items_icon_size * inv_zoom;
    float scaled_separator_size = m_layout.separator_size * inv_zoom;
    float scaled_gap_size = m_layout.gap_size * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;

    int id = -1;

    for (GLToolbarItem* item : m_items)
    {
        ++id;

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

void GLToolbar::render_horizontal() const
{
    unsigned int tex_id = m_icons_texture.texture.get_id();
    int tex_size = m_icons_texture.texture.get_width();

    if ((tex_id == 0) || (tex_size <= 0))
        return;

    float zoom = m_parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    float scaled_icons_size = (float)m_icons_texture.items_icon_size * inv_zoom;
    float scaled_separator_size = m_layout.separator_size * inv_zoom;
    float scaled_gap_size = m_layout.gap_size * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;

    // renders icons
    for (const GLToolbarItem* item : m_items)
    {
        if (item->is_separator())
            left += separator_stride;
        else
        {
            item->render(tex_id, left, left + scaled_icons_size, top - scaled_icons_size, top, (unsigned int)tex_size, m_icons_texture.items_icon_border_size, m_icons_texture.items_icon_size, m_icons_texture.items_icon_gap_size);
            left += icon_stride;
        }
    }
}

void GLToolbar::render_vertical() const
{
    unsigned int tex_id = m_icons_texture.texture.get_id();
    int tex_size = m_icons_texture.texture.get_width();

    if ((tex_id == 0) || (tex_size <= 0))
        return;

    float zoom = m_parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    float scaled_icons_size = (float)m_icons_texture.items_icon_size * inv_zoom;
    float scaled_separator_size = m_layout.separator_size * inv_zoom;
    float scaled_gap_size = m_layout.gap_size * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;

    // renders icons
    for (const GLToolbarItem* item : m_items)
    {
        if (item->is_separator())
            top -= separator_stride;
        else
        {
            item->render(tex_id, left, left + scaled_icons_size, top - scaled_icons_size, top, (unsigned int)tex_size, m_icons_texture.items_icon_border_size, m_icons_texture.items_icon_size, m_icons_texture.items_icon_gap_size);
            top -= icon_stride;
        }
    }
}

GLRadioToolbarItem::Data::Data()
    : name("")
    , tooltip("")
    , sprite_id(-1)
{
}

GLRadioToolbarItem::GLRadioToolbarItem(const GLRadioToolbarItem::Data& data)
    : m_state(Normal)
    , m_data(data)
{
}

GLRadioToolbarItem::EState GLRadioToolbarItem::get_state() const
{
    return m_state;
}

void GLRadioToolbarItem::set_state(GLRadioToolbarItem::EState state)
{
    m_state = state;
}

const std::string& GLRadioToolbarItem::get_name() const
{
    return m_data.name;
}

const std::string& GLRadioToolbarItem::get_tooltip() const
{
    return m_data.tooltip;
}

bool GLRadioToolbarItem::is_hovered() const
{
    return (m_state == Hover) || (m_state == HoverPressed);
}

bool GLRadioToolbarItem::is_pressed() const
{
    return (m_state == Pressed) || (m_state == HoverPressed);
}

void GLRadioToolbarItem::do_action(wxEvtHandler *target)
{
    wxPostEvent(target, SimpleEvent(m_data.action_event));
}

void GLRadioToolbarItem::render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const
{
    GLTexture::render_sub_texture(tex_id, left, right, bottom, top, get_uvs(texture_size, border_size, icon_size, gap_size));
}

GLTexture::Quad_UVs GLRadioToolbarItem::get_uvs(unsigned int texture_size, unsigned int border_size, unsigned int icon_size, unsigned int gap_size) const
{
    GLTexture::Quad_UVs uvs;

    float inv_texture_size = (texture_size != 0) ? 1.0f / (float)texture_size : 0.0f;

    float scaled_icon_size = (float)icon_size * inv_texture_size;
    float scaled_border_size = (float)border_size * inv_texture_size;
    float scaled_gap_size = (float)gap_size * inv_texture_size;
    float stride = scaled_icon_size + scaled_gap_size;

    float left = scaled_border_size + (float)m_state * stride;
    float right = left + scaled_icon_size;
    float top = scaled_border_size + (float)m_data.sprite_id * stride;
    float bottom = top + scaled_icon_size;

    uvs.left_top = { left, top };
    uvs.left_bottom = { left, bottom };
    uvs.right_bottom = { right, bottom };
    uvs.right_top = { right, top };

    return uvs;
}

GLRadioToolbar::GLRadioToolbar()
    : m_top(0.0f)
    , m_left(0.0f)
{
}

GLRadioToolbar::~GLRadioToolbar()
{
    for (GLRadioToolbarItem* item : m_items)
    {
        delete item;
    }
}

bool GLRadioToolbar::init(const std::string& icons_texture_filename, unsigned int items_icon_size, unsigned int items_icon_border_size, unsigned int items_icon_gap_size)
{
    if (m_icons_texture.texture.get_id() != 0)
        return true;

    std::string path = resources_dir() + "/icons/";
    bool res = !icons_texture_filename.empty() && m_icons_texture.texture.load_from_file(path + icons_texture_filename, false);
    if (res)
    {
        m_icons_texture.items_icon_size = items_icon_size;
        m_icons_texture.items_icon_border_size = items_icon_border_size;
        m_icons_texture.items_icon_gap_size = items_icon_gap_size;
    }

    return res;
}

bool GLRadioToolbar::add_item(const GLRadioToolbarItem::Data& data)
{
    GLRadioToolbarItem* item = new GLRadioToolbarItem(data);
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    return true;
}

float GLRadioToolbar::get_height() const
{
    return m_icons_texture.items_icon_size;
}

void GLRadioToolbar::set_position(float top, float left)
{
    m_top = top;
    m_left = left;
}

void GLRadioToolbar::set_selection(const std::string& name)
{
    for (GLRadioToolbarItem* item : m_items)
    {
        item->set_state((item->get_name() == name) ? GLRadioToolbarItem::Pressed : GLRadioToolbarItem::Normal);
    }
}

int GLRadioToolbar::contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    float zoom = parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.items_icon_size * inv_zoom;

    float left = m_left;
    float top = m_top;

    int id = -1;

    for (GLRadioToolbarItem* item : m_items)
    {
        ++id;

        float right = left + scaled_icons_size;
        float bottom = top - scaled_icons_size;

        if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
            return id;

        left += scaled_icons_size;
    }

    return -1;
}

std::string GLRadioToolbar::update_hover_state(const Vec2d& mouse_pos, const GLCanvas3D& parent)
{
    float zoom = parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = (float)m_icons_texture.items_icon_size * inv_zoom;

    float left = m_left;
    float top = m_top;

    std::string tooltip = "";

    for (GLRadioToolbarItem* item : m_items)
    {
        float right = left + scaled_icons_size;
        float bottom = top - scaled_icons_size;

        GLRadioToolbarItem::EState state = item->get_state();
        bool inside = (left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top);

        switch (state)
        {
        case GLRadioToolbarItem::Normal:
        {
            if (inside)
                item->set_state(GLRadioToolbarItem::Hover);

            break;
        }
        case GLRadioToolbarItem::Hover:
        {
            if (inside)
                tooltip = item->get_tooltip();
            else
                item->set_state(GLRadioToolbarItem::Normal);

            break;
        }
        case GLRadioToolbarItem::Pressed:
        {
            if (inside)
                item->set_state(GLRadioToolbarItem::HoverPressed);

            break;
        }
        case GLRadioToolbarItem::HoverPressed:
        {
            if (inside)
                tooltip = item->get_tooltip();
            else
                item->set_state(GLRadioToolbarItem::Pressed);

            break;
        }
        default:
        {
            break;
        }
        }

        left += scaled_icons_size;
    }

    return tooltip;
}

void GLRadioToolbar::do_action(unsigned int item_id, GLCanvas3D& parent)
{
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
    {
        if (i != item_id)
            m_items[i]->set_state(GLRadioToolbarItem::Normal);
    }

    if (item_id < (unsigned int)m_items.size())
    {
        GLRadioToolbarItem* item = m_items[item_id];
        if ((item != nullptr) && item->is_hovered() && !item->is_pressed())
        {
            item->set_state(GLRadioToolbarItem::HoverPressed);
            item->do_action(parent.get_wxglcanvas());
        }
    }

    parent.set_as_dirty();
}

void GLRadioToolbar::render(const GLCanvas3D& parent) const
{
    if (m_items.empty())
        return;

    ::glDisable(GL_DEPTH_TEST);

    ::glPushMatrix();
    ::glLoadIdentity();

    unsigned int tex_id = m_icons_texture.texture.get_id();
    int tex_size = m_icons_texture.texture.get_width();

    if ((tex_id == 0) || (tex_size <= 0))
        return;

    float zoom = parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    float scaled_icons_size = (float)m_icons_texture.items_icon_size * inv_zoom;

    float left = m_left;
    float top = m_top;

    // renders icons
    for (const GLRadioToolbarItem* item : m_items)
    {
        item->render(tex_id, left, left + scaled_icons_size, top - scaled_icons_size, top, (unsigned int)tex_size, m_icons_texture.items_icon_border_size, m_icons_texture.items_icon_size, m_icons_texture.items_icon_gap_size);
        left += scaled_icons_size;
    }

    ::glPopMatrix();
}

} // namespace GUI
} // namespace Slic3r
