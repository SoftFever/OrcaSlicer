#include "libslic3r/libslic3r.h"
#include "GLTexture.hpp"

#include <GL/glew.h>

#include <wx/image.h>

#include <boost/filesystem.hpp>
#if ENABLE_TEXTURES_FROM_SVG
#include <boost/algorithm/string/predicate.hpp>
#endif // ENABLE_TEXTURES_FROM_SVG

#include <vector>
#include <algorithm>

#if ENABLE_TEXTURES_FROM_SVG
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
#endif // ENABLE_TEXTURES_FROM_SVG

#include "libslic3r/Utils.hpp"

namespace Slic3r {
namespace GUI {

GLTexture::Quad_UVs GLTexture::FullTextureUVs = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };

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

#if ENABLE_TEXTURES_FROM_SVG
bool GLTexture::load_from_file(const std::string& filename, bool use_mipmaps)
{
    reset();

    if (!boost::filesystem::exists(filename))
        return false;

    if (boost::algorithm::iends_with(filename, ".png"))
        return load_from_png(filename, use_mipmaps);
    else
        return false;
}

bool GLTexture::load_from_svg_file(const std::string& filename, bool use_mipmaps, unsigned int max_size_px)
{
    reset();

    if (!boost::filesystem::exists(filename))
        return false;

    if (boost::algorithm::iends_with(filename, ".svg"))
        return load_from_svg(filename, use_mipmaps, max_size_px);
    else
        return false;
}
#else
bool GLTexture::load_from_file(const std::string& filename, bool use_mipmaps)
{
    reset();

    if (!boost::filesystem::exists(filename))
        return false;

    // Load a PNG with an alpha channel.
    wxImage image;
	if (!image.LoadFile(wxString::FromUTF8(filename.c_str()), wxBITMAP_TYPE_PNG))
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
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ::glGenTextures(1, &m_id);
    ::glBindTexture(GL_TEXTURE_2D, m_id);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    if (use_mipmaps)
    {
        // we manually generate mipmaps because glGenerateMipmap() function is not reliable on all graphics cards
        unsigned int levels_count = generate_mipmaps(image);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1 + levels_count);
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
#endif // ENABLE_TEXTURES_FROM_SVG

void GLTexture::reset()
{
    if (m_id != 0)
        ::glDeleteTextures(1, &m_id);

    m_id = 0;
    m_width = 0;
    m_height = 0;
    m_source = "";
}

void GLTexture::render_texture(unsigned int tex_id, float left, float right, float bottom, float top)
{
    render_sub_texture(tex_id, left, right, bottom, top, FullTextureUVs);
}

void GLTexture::render_sub_texture(unsigned int tex_id, float left, float right, float bottom, float top, const GLTexture::Quad_UVs& uvs)
{
    ::glEnable(GL_BLEND);
    ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ::glEnable(GL_TEXTURE_2D);
    ::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    ::glBindTexture(GL_TEXTURE_2D, (GLuint)tex_id);

    ::glBegin(GL_QUADS);
    ::glTexCoord2f(uvs.left_bottom.u, uvs.left_bottom.v); ::glVertex2f(left, bottom);
    ::glTexCoord2f(uvs.right_bottom.u, uvs.right_bottom.v); ::glVertex2f(right, bottom);
    ::glTexCoord2f(uvs.right_top.u, uvs.right_top.v); ::glVertex2f(right, top);
    ::glTexCoord2f(uvs.left_top.u, uvs.left_top.v); ::glVertex2f(left, top);
    ::glEnd();

    ::glBindTexture(GL_TEXTURE_2D, 0);

    ::glDisable(GL_TEXTURE_2D);
    ::glDisable(GL_BLEND);
}

unsigned int GLTexture::generate_mipmaps(wxImage& image)
{
    int w = image.GetWidth();
    int h = image.GetHeight();
    GLint level = 0;
    std::vector<unsigned char> data(w * h * 4, 0);

    while ((w > 1) || (h > 1))
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

        ::glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    }

    return (unsigned int)level;
}

#if ENABLE_TEXTURES_FROM_SVG
bool GLTexture::load_from_png(const std::string& filename, bool use_mipmaps)
{
    // Load a PNG with an alpha channel.
    wxImage image;
    if (!image.LoadFile(wxString::FromUTF8(filename.c_str()), wxBITMAP_TYPE_PNG))
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
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ::glGenTextures(1, &m_id);
    ::glBindTexture(GL_TEXTURE_2D, m_id);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    if (use_mipmaps)
    {
        // we manually generate mipmaps because glGenerateMipmap() function is not reliable on all graphics cards
        unsigned int levels_count = generate_mipmaps(image);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1 + levels_count);
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

bool GLTexture::load_from_svg(const std::string& filename, bool use_mipmaps, unsigned int max_size_px)
{
    NSVGimage* image = nsvgParseFromFile(filename.c_str(), "px", 96.0f);
    if (image == nullptr)
    {
//        printf("Could not open SVG image.\n");
        reset();
        return false;
    }

    float scale = (float)max_size_px / std::max(image->width, image->height);

    m_width = (int)(scale * image->width);
    m_height = (int)(scale * image->height);
    int n_pixels = m_width * m_height;

    if (n_pixels <= 0)
    {
        reset();
        return false;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (rast == nullptr)
    {
//        printf("Could not init rasterizer.\n");
        nsvgDelete(image);
        reset();
        return false;
    }

    // creates the temporary buffer only once, with max size, and reuse it for all the levels, if generating mipmaps
    std::vector<unsigned char> data(n_pixels * 4, 0);
    nsvgRasterize(rast, image, 0, 0, scale, data.data(), m_width, m_height, m_width * 4);

    // sends data to gpu
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ::glGenTextures(1, &m_id);
    ::glBindTexture(GL_TEXTURE_2D, m_id);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    if (use_mipmaps)
    {
        // we manually generate mipmaps because glGenerateMipmap() function is not reliable on all graphics cards
        int lod_w = m_width;
        int lod_h = m_height;
        GLint level = 0;
        while ((lod_w > 1) || (lod_h > 1))
        {
            ++level;

            lod_w = std::max(lod_w / 2, 1);
            lod_h = std::max(lod_h / 2, 1);
            scale /= 2.0f;

            nsvgRasterize(rast, image, 0, 0, scale, data.data(), lod_w, lod_h, lod_w * 4);
            ::glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, (GLsizei)lod_w, (GLsizei)lod_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
        }

        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1 + level);
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

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if 1
    // debug output
    wxImage output(m_width, m_height);
    output.InitAlpha();

    for (int h = 0; h < m_height; ++h)
    {
        int px_h = h * m_width;
        for (int w = 0; w < m_width; ++w)
        {
            int offset = (px_h + w) * 4;
            output.SetRGB(w, h, data.data()[offset + 0], data.data()[offset + 1], data.data()[offset + 2]);
            output.SetAlpha(w, h, data.data()[offset + 3]);
        }
    }

    output.SaveFile(resources_dir() + "/icons/test_svg_import.png", wxBITMAP_TYPE_PNG);
#endif // 0
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    return true;
}
#endif // ENABLE_TEXTURES_FROM_SVG

} // namespace GUI
} // namespace Slic3r
