#include "GLTexture.hpp"

#include <GL/glew.h>

#include <wx/image.h>

#include <boost/filesystem.hpp>

#include <vector>
#include <algorithm>

namespace Slic3r {
namespace GUI {

GLTexture::GLTexture()
    : m_id(0)
    , m_width(0)
    , m_height(0)
    , m_source("")
{
}

GLTexture::~GLTexture()
{
    reset();
}

bool GLTexture::load_from_file(const std::string& filename, bool generate_mipmaps)
{
    reset();

    if (!boost::filesystem::exists(filename))
        return false;

    // Load a PNG with an alpha channel.
    wxImage image;
    if (!image.LoadFile(filename, wxBITMAP_TYPE_PNG))
    {
        reset();
        return false;
    }

    m_width = image.GetWidth();
    m_height = image.GetHeight();
    int n_pixels = m_width * m_height;

    if (n_pixels <= 0)
    {
        reset();
        return false;
    }

    // Get RGB & alpha raw data from wxImage, pack them into an array.
    unsigned char* img_rgb = image.GetData();
    if (img_rgb == nullptr)
    {
        reset();
        return false;
    }

    unsigned char* img_alpha = image.GetAlpha();

    std::vector<unsigned char> data(n_pixels * 4, 0);
    for (int i = 0; i < n_pixels; ++i)
    {
        int data_id = i * 4;
        int img_id = i * 3;
        data[data_id + 0] = img_rgb[img_id + 0];
        data[data_id + 1] = img_rgb[img_id + 1];
        data[data_id + 2] = img_rgb[img_id + 2];
        data[data_id + 3] = (img_alpha != nullptr) ? img_alpha[i] : 255;
    }

    // sends data to gpu
    ::glGenTextures(1, &m_id);
    ::glBindTexture(GL_TEXTURE_2D, m_id);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    if (generate_mipmaps)
    {
        // we manually generate mipmaps because glGenerateMipmap() function is not reliable on all graphics cards
        _generate_mipmaps(image);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    else
    {
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    }
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    ::glBindTexture(GL_TEXTURE_2D, 0);

    m_source = filename;
    return true;
}

void GLTexture::reset()
{
    if (m_id != 0)
        ::glDeleteTextures(1, &m_id);

    m_id = 0;
    m_width = 0;
    m_height = 0;
    m_source = "";
}

unsigned int GLTexture::get_id() const
{
    return m_id;
}

int GLTexture::get_width() const
{
    return m_width;
}

int GLTexture::get_height() const
{
    return m_height;
}

const std::string& GLTexture::get_source() const
{
    return m_source;
}

void GLTexture::render_texture(unsigned int tex_id, float left, float right, float bottom, float top)
{
    ::glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    ::glDisable(GL_LIGHTING);
    ::glEnable(GL_BLEND);
    ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ::glEnable(GL_TEXTURE_2D);

    ::glBindTexture(GL_TEXTURE_2D, (GLuint)tex_id);

    ::glBegin(GL_QUADS);
    ::glTexCoord2d(0.0f, 1.0f); ::glVertex3f(left, bottom, 0.0f);
    ::glTexCoord2d(1.0f, 1.0f); ::glVertex3f(right, bottom, 0.0f);
    ::glTexCoord2d(1.0f, 0.0f); ::glVertex3f(right, top, 0.0f);
    ::glTexCoord2d(0.0f, 0.0f); ::glVertex3f(left, top, 0.0f);
    ::glEnd();

    ::glBindTexture(GL_TEXTURE_2D, 0);

    ::glDisable(GL_TEXTURE_2D);
    ::glDisable(GL_BLEND);
    ::glEnable(GL_LIGHTING);
}

void GLTexture::_generate_mipmaps(wxImage& image)
{
    int w = image.GetWidth();
    int h = image.GetHeight();
    GLint level = 0;
    std::vector<unsigned char> data(w * h * 4, 0);

    while ((w > 1) && (h > 1))
    {
        ++level;

        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);

        int n_pixels = w * h;

        image = image.ResampleBicubic(w, h);

        unsigned char* img_rgb = image.GetData();
        unsigned char* img_alpha = image.GetAlpha();

        data.resize(n_pixels * 4);
        for (int i = 0; i < n_pixels; ++i)
        {
            int data_id = i * 4;
            int img_id = i * 3;
            data[data_id + 0] = img_rgb[img_id + 0];
            data[data_id + 1] = img_rgb[img_id + 1];
            data[data_id + 2] = img_rgb[img_id + 2];
            data[data_id + 3] = (img_alpha != nullptr) ? img_alpha[i] : 255;
        }

        ::glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    }
}

} // namespace GUI
} // namespace Slic3r
