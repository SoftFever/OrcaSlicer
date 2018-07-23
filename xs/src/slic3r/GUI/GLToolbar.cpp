#include "GLToolbar.hpp"

#include "../../libslic3r/Utils.hpp"
#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>

namespace Slic3r {
namespace GUI {

const unsigned char GLToolbarItem::TooltipTexture::Border_Color[3] = { 0, 0, 0 };
const int GLToolbarItem::TooltipTexture::Border_Offset = 5;

bool GLToolbarItem::TooltipTexture::generate(const std::string& text)
{
    reset();

    if (text.empty())
        return false;

    wxMemoryDC memDC;
    // select default font
    memDC.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    // calculates texture size
    wxCoord w, h;
    memDC.GetTextExtent(text, &w, &h);
    m_width = (int)w + 2 * Border_Offset;
    m_height = (int)h + 2 * Border_Offset;

    // generates bitmap
    wxBitmap bitmap(m_width, m_height);

    memDC.SelectObject(bitmap);
    memDC.SetBackground(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
    memDC.Clear();

    // draw message
    memDC.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));
    memDC.DrawText(text, (wxCoord)Border_Offset, (wxCoord)Border_Offset);

    wxPen pen(wxSystemSettings::GetColour(wxSYS_COLOUR_ACTIVEBORDER));
    memDC.SetPen(pen);
    wxCoord ww = (wxCoord)m_width - 1;
    wxCoord hh = (wxCoord)m_height - 1;
    memDC.DrawLine(0, 0, ww, 0);
    memDC.DrawLine(ww, 0, ww, hh);
    memDC.DrawLine(ww, hh, 0, hh);
    memDC.DrawLine(0, hh, 0, 0);

    memDC.SelectObject(wxNullBitmap);

    // Convert the bitmap into a linear data ready to be loaded into the GPU.
    wxImage image = bitmap.ConvertToImage();

    // prepare buffer
    std::vector<unsigned char> data(4 * m_width * m_height, 0);
    for (int h = 0; h < m_height; ++h)
    {
        int hh = h * m_width;
        unsigned char* px_ptr = data.data() + 4 * hh;
        for (int w = 0; w < m_width; ++w)
        {
            *px_ptr++ = image.GetRed(w, h);
            *px_ptr++ = image.GetGreen(w, h);
            *px_ptr++ = image.GetBlue(w, h);
            *px_ptr++ = 255;
        }
    }

    // sends buffer to gpu
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ::glGenTextures(1, &m_id);
    ::glBindTexture(GL_TEXTURE_2D, (GLuint)m_id);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    ::glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

GLToolbarItem::GLToolbarItem(EType type, const std::string& name, const std::string& tooltip)
    : m_type(type)
    , m_state(Disabled)
    , m_name(name)
    , m_tooltip(tooltip)
    , m_tooltip_shown(false)
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

    if ((m_type == Action) && !m_tooltip.empty())
    {
        if (!m_tooltip_texture.generate(m_tooltip))
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

void GLToolbarItem::show_tooltip()
{
    m_tooltip_shown = true;
}

void GLToolbarItem::hide_tooltip()
{
    m_tooltip_shown = false;
}

bool GLToolbarItem::is_tooltip_shown() const
{
    return m_tooltip_shown && (m_tooltip_texture.get_id() > 0);
}

unsigned int GLToolbarItem::get_icon_texture_id() const
{
    return m_icon_textures[m_state].get_id();
}

int GLToolbarItem::get_icon_textures_size() const
{
    return m_icon_textures[Normal].get_width();
}

unsigned int GLToolbarItem::get_tooltip_texture_id() const
{
    return m_tooltip_texture.get_id();
}

int GLToolbarItem::get_tooltip_texture_width() const
{
    return m_tooltip_texture.get_width();
}

int GLToolbarItem::get_tooltip_texture_height() const
{
    return m_tooltip_texture.get_height();
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

    if (data.name == "add")
        item->show_tooltip();

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

void GLToolbar::update_hover_state(const GLCanvas3D& canvas, const Pointf& mouse_pos)
{
    if (!m_enabled)
        return;

    float cnv_w = (float)canvas.get_canvas_size().get_width();
    float width = _get_total_width();
    float left = 0.5f * (cnv_w - width);
    float top = m_offset_y;

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

    // renders tooltip
    for (const GLToolbarItem* item : m_items)
    {
        if (!item->is_separator() && item->is_tooltip_shown())
        {
            float l = (-0.5f * cnv_w + (float)mouse_pos.x) * inv_zoom;
            float r = l + (float)item->get_tooltip_texture_width() * inv_zoom;
            float t = (0.5f * cnv_h - (float)mouse_pos.y) * inv_zoom;
            float b = t - (float)item->get_tooltip_texture_height() * inv_zoom;
            GLTexture::render_texture(item->get_tooltip_texture_id(), l, r, b, t);
            break;
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
