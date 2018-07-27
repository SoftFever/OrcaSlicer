#include "GLToolbar.hpp"

#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>

namespace Slic3r {
namespace GUI {

GLToolbarItem::GLToolbarItem(EType type, const std::string& name, const std::string& tooltip, bool is_toggable, PerlCallback* action_callback)
    : m_type(type)
    , m_state(Disabled)
    , m_name(name)
    , m_tooltip(tooltip)
    , m_is_toggable(is_toggable)
    , m_action_callback(action_callback)
{
}

bool GLToolbarItem::load_textures(const std::string* filenames)
{
    if (filenames == nullptr)
        return false;

    std::string path = resources_dir() + "/icons/";

    for (unsigned int i = (unsigned int)Normal; i < (unsigned int)Num_States; ++i)
    {
        if (!filenames[i].empty())
        {
            std::string filename = path + filenames[i];
            if (!m_icon_textures[i].load_from_file(filename, false))
                return false;
        }
    }

    return true;
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
    return m_name;
}

const std::string& GLToolbarItem::get_tooltip() const
{
    return m_tooltip;
}

unsigned int GLToolbarItem::get_icon_texture_id() const
{
    return m_icon_textures[m_state].get_id();
}

int GLToolbarItem::get_icon_textures_size() const
{
    return m_icon_textures[Normal].get_width();
}

void GLToolbarItem::do_action()
{
    if (m_action_callback != nullptr)
        m_action_callback->call();
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
    return m_is_toggable;
}

bool GLToolbarItem::is_separator() const
{
    return m_type == Separator;
}

GLToolbar::GLToolbar(GLCanvas3D& parent)
    : m_parent(parent)
    , m_enabled(false)
    , m_textures_scale(1.0f)
    , m_offset_y(5.0f)
    , m_gap_x(2.0f)
    , m_separator_x(5.0f)
{
}

bool GLToolbar::is_enabled() const
{
    return m_enabled;
}

void GLToolbar::set_enabled(bool enable)
{
    m_enabled = true;
}

void GLToolbar::set_textures_scale(float scale)
{
    m_textures_scale = scale;
}

void GLToolbar::set_offset_y(float offset)
{
    m_offset_y = offset;
}

void GLToolbar::set_gap_x(float gap)
{
    m_gap_x = gap;
}

void GLToolbar::set_separator_x(float separator)
{
    m_separator_x = separator;
}

bool GLToolbar::add_item(const GLToolbar::ItemCreationData& data)
{
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Action, data.name, data.tooltip, data.is_toggable, data.action_callback);
    if ((item == nullptr) || !item->load_textures(data.textures))
        return false;

    m_items.push_back(item);

    return true;
}

bool GLToolbar::add_separator()
{
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Separator, "", "", false, nullptr);
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    return true;
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

void GLToolbar::update_hover_state(const Pointf& mouse_pos)
{
    if (!m_enabled)
        return;

    float cnv_w = (float)m_parent.get_canvas_size().get_width();
    float width = _get_total_width();
    float left = 0.5f * (cnv_w - width);
    float top = m_offset_y;

    std::string tooltip = "";

    for (GLToolbarItem* item : m_items)
    {
        if (item->is_separator())
            left += (m_separator_x + m_gap_x);
        else
        {
            float tex_size = (float)item->get_icon_textures_size() * m_textures_scale;
            float right = left + tex_size;
            float bottom = top + tex_size;

            GLToolbarItem::EState state = item->get_state();
            bool inside = (left <= mouse_pos.x) && (mouse_pos.x <= right) && (top <= mouse_pos.y) && (mouse_pos.y <= bottom);

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
            left += (tex_size + m_gap_x);
        }
    }

    m_parent.set_tooltip(tooltip);
}

int GLToolbar::contains_mouse(const Pointf& mouse_pos) const
{
    if (!m_enabled)
        return -1;

    float cnv_w = (float)m_parent.get_canvas_size().get_width();
    float width = _get_total_width();
    float left = 0.5f * (cnv_w - width);
    float top = m_offset_y;

    int id = -1;

    for (GLToolbarItem* item : m_items)
    {
        ++id;

        if (item->is_separator())
            left += (m_separator_x + m_gap_x);
        else
        {
            float tex_size = (float)item->get_icon_textures_size() * m_textures_scale;
            float right = left + tex_size;
            float bottom = top + tex_size;

            if ((left <= mouse_pos.x) && (mouse_pos.x <= right) && (top <= mouse_pos.y) && (mouse_pos.y <= bottom))
                return id;

            left += (tex_size + m_gap_x);
        }
    }

    return -1;
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
                item->do_action();
            }
            else
            {
                item->set_state(GLToolbarItem::HoverPressed);
                m_parent.render();
                item->do_action();
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

void GLToolbar::render(const Pointf& mouse_pos) const
{
    if (!m_enabled || m_items.empty())
        return;

    ::glDisable(GL_DEPTH_TEST);

    ::glPushMatrix();
    ::glLoadIdentity();

    float cnv_w = (float)m_parent.get_canvas_size().get_width();
    float cnv_h = (float)m_parent.get_canvas_size().get_height();
    float zoom = m_parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    float width = _get_total_width();
    float top_x = -0.5f * width * inv_zoom;
    float top_y = (0.5f * cnv_h - m_offset_y * m_textures_scale) * inv_zoom;
    float scaled_gap_x = m_gap_x * inv_zoom;
    float scaled_separator_x = m_separator_x * inv_zoom;

    // renders icons
    for (const GLToolbarItem* item : m_items)
    {
        if (item->is_separator())
            top_x += (scaled_separator_x + scaled_gap_x);
        else
        {
            float tex_size = (float)item->get_icon_textures_size() * m_textures_scale * inv_zoom;
            GLTexture::render_texture(item->get_icon_texture_id(), top_x, top_x + tex_size, top_y - tex_size, top_y);
            top_x += (tex_size + scaled_gap_x);
        }
    }

    ::glPopMatrix();
}

float GLToolbar::_get_total_width() const
{
    float width = 0.0f;

    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
    {
        if (m_items[i]->is_separator())
            width += m_separator_x;
        else
            width += m_items[i]->get_icon_textures_size();

        if (i < (unsigned int)m_items.size() - 1)
            width += m_gap_x;
    }

    return width;
}

} // namespace GUI
} // namespace Slic3r
