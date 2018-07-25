#include "GLToolbar.hpp"

#include "../../libslic3r/Utils.hpp"
#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>

namespace Slic3r {
namespace GUI {

GLToolbarItem::GLToolbarItem(EType type, const std::string& name, const std::string& tooltip)
    : m_type(type)
    , m_state(Disabled)
    , m_name(name)
    , m_tooltip(tooltip)
{
}

bool GLToolbarItem::load_textures(const std::string* filenames)
{
    if (filenames == nullptr)
        return false;

    std::string path = resources_dir() + "/icons/";

    for (unsigned int i = (unsigned int)Normal; i < (unsigned int)Num_States; ++i)
    {
        std::string filename = path + filenames[i];
        if (!m_icon_textures[i].load_from_file(filename, false))
            return false;
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

bool GLToolbarItem::is_separator() const
{
    return m_type == Separator;
}

GLToolbar::GLToolbar()
    : m_enabled(false)
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
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Action, data.name, data.tooltip);
    if ((item == nullptr) || !item->load_textures(data.textures))
        return false;

    m_items.push_back(item);

    return true;
}

bool GLToolbar::add_separator()
{
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Separator, "", "");
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    return true;
}

void GLToolbar::enable_item(const std::string& name)
{
    for (GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
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

void GLToolbar::update_hover_state(GLCanvas3D& canvas, const Pointf& mouse_pos)
{
    if (!m_enabled)
        return;

    float cnv_w = (float)canvas.get_canvas_size().get_width();
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
                if (!inside)
                    item->set_state(GLToolbarItem::Normal);

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

    canvas.set_tooltip(tooltip);
}

void GLToolbar::render(const GLCanvas3D& canvas, const Pointf& mouse_pos) const
{
    if (m_items.empty())
        return;

    ::glDisable(GL_DEPTH_TEST);

    ::glPushMatrix();
    ::glLoadIdentity();

    float cnv_w = (float)canvas.get_canvas_size().get_width();
    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float zoom = canvas.get_camera_zoom();
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
