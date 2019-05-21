#include "libslic3r/libslic3r.h"
#include "GLTexture.hpp"

#include "3DScene.hpp"

#include <GL/glew.h>

#include <wx/image.h>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <vector>
#include <algorithm>

#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

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

bool GLTexture::load_from_svg_files_as_sprites_array(const std::vector<std::string>& filenames, const std::vector<std::pair<int, bool>>& states, unsigned int sprite_size_px)
{
    reset();

    if (filenames.empty() || states.empty() || (sprite_size_px == 0))
        return false;

    m_width = (int)(sprite_size_px * states.size());
    m_height = (int)(sprite_size_px * filenames.size());
    int n_pixels = m_width * m_height;
    int sprite_n_pixels = sprite_size_px * sprite_size_px;
    int sprite_bytes = sprite_n_pixels * 4;
    int sprite_stride = sprite_size_px * 4;

    if (n_pixels <= 0)
    {
        reset();
        return false;
    }

    std::vector<unsigned char> data(n_pixels * 4, 0);
    std::vector<unsigned char> sprite_data(sprite_bytes, 0);
    std::vector<unsigned char> sprite_white_only_data(sprite_bytes, 0);
    std::vector<unsigned char> sprite_gray_only_data(sprite_bytes, 0);
    std::vector<unsigned char> output_data(sprite_bytes, 0);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (rast == nullptr)
    {
        reset();
        return false;
    }

    int sprite_id = -1;
    for (const std::string& filename : filenames)
    {
        ++sprite_id;

        if (!boost::filesystem::exists(filename))
            continue;

        if (!boost::algorithm::iends_with(filename, ".svg"))
            continue;

        NSVGimage* image = nsvgParseFromFile(filename.c_str(), "px", 96.0f);
        if (image == nullptr)
            continue;

        float scale = (float)sprite_size_px / std::max(image->width, image->height);

        nsvgRasterize(rast, image, 0, 0, scale, sprite_data.data(), sprite_size_px, sprite_size_px, sprite_stride);

        // makes white only copy of the sprite
        ::memcpy((void*)sprite_white_only_data.data(), (const void*)sprite_data.data(), sprite_bytes);
        for (int i = 0; i < sprite_n_pixels; ++i)
        {
            int offset = i * 4;
            if (sprite_white_only_data.data()[offset] != 0)
                ::memset((void*)&sprite_white_only_data.data()[offset], 255, 3);
        }

        // makes gray only copy of the sprite
        ::memcpy((void*)sprite_gray_only_data.data(), (const void*)sprite_data.data(), sprite_bytes);
        for (int i = 0; i < sprite_n_pixels; ++i)
        {
            int offset = i * 4;
            if (sprite_gray_only_data.data()[offset] != 0)
                ::memset((void*)&sprite_gray_only_data.data()[offset], 128, 3);
        }

        int sprite_offset_px = sprite_id * sprite_size_px * m_width;
        int state_id = -1;
        for (const std::pair<int, bool>& state : states)
        {
            ++state_id;

            // select the sprite variant
            std::vector<unsigned char>* src = nullptr;
            switch (state.first)
            {
            case 1: { src = &sprite_white_only_data; break; }
            case 2: { src = &sprite_gray_only_data; break; }
            default: { src = &sprite_data; break; }
            }

            ::memcpy((void*)output_data.data(), (const void*)src->data(), sprite_bytes);
            // applies background, if needed
            if (state.second)
            {
                for (int i = 0; i < sprite_n_pixels; ++i)
                {
                    int offset = i * 4;
                    float alpha = (float)output_data.data()[offset + 3] / 255.0f;
                    output_data.data()[offset + 0] = (unsigned char)(output_data.data()[offset + 0] * alpha);
                    output_data.data()[offset + 1] = (unsigned char)(output_data.data()[offset + 1] * alpha);
                    output_data.data()[offset + 2] = (unsigned char)(output_data.data()[offset + 2] * alpha);
                    output_data.data()[offset + 3] = (unsigned char)(128 * (1.0f - alpha) + output_data.data()[offset + 3] * alpha);
                }
            }

            int state_offset_px = sprite_offset_px + state_id * sprite_size_px;
            for (int j = 0; j < (int)sprite_size_px; ++j)
            {
                ::memcpy((void*)&data.data()[(state_offset_px + j * m_width) * 4], (const void*)&output_data.data()[j * sprite_stride], sprite_stride);
            }
        }

        nsvgDelete(image);
    }

    nsvgDeleteRasterizer(rast);

    // sends data to gpu
    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glGenTextures(1, &m_id));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_id));
#if ENABLE_COMPRESSED_TEXTURES
    if (GLEW_EXT_texture_compression_s3tc)
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#else
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#endif // ENABLE_COMPRESSED_TEXTURES
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    m_source = filenames.front();
    
#if 0
    // debug output
    static int pass = 0;
    ++pass;

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

    std::string out_filename = resources_dir() + "/icons/test_" + std::to_string(pass) + ".png";
    output.SaveFile(out_filename, wxBITMAP_TYPE_PNG);
#endif // 0

    return true;
}

void GLTexture::reset()
{
    if (m_id != 0)
        glsafe(::glDeleteTextures(1, &m_id));

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
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    glsafe(::glEnable(GL_TEXTURE_2D));
    glsafe(::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));

    glsafe(::glBindTexture(GL_TEXTURE_2D, (GLuint)tex_id));

    ::glBegin(GL_QUADS);
    ::glTexCoord2f(uvs.left_bottom.u, uvs.left_bottom.v); ::glVertex2f(left, bottom);
    ::glTexCoord2f(uvs.right_bottom.u, uvs.right_bottom.v); ::glVertex2f(right, bottom);
    ::glTexCoord2f(uvs.right_top.u, uvs.right_top.v); ::glVertex2f(right, top);
    ::glTexCoord2f(uvs.left_top.u, uvs.left_top.v); ::glVertex2f(left, top);
    glsafe(::glEnd());

    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    glsafe(::glDisable(GL_TEXTURE_2D));
    glsafe(::glDisable(GL_BLEND));
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

#if ENABLE_COMPRESSED_TEXTURES
        if (GLEW_EXT_texture_compression_s3tc)
            glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
        else
            glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#endif // ENABLE_COMPRESSED_TEXTURES
    }

    return (unsigned int)level;
}

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
    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glGenTextures(1, &m_id));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_id));
#if ENABLE_COMPRESSED_TEXTURES
    if (GLEW_EXT_texture_compression_s3tc)
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#else
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#endif // ENABLE_COMPRESSED_TEXTURES
    if (use_mipmaps)
    {
        // we manually generate mipmaps because glGenerateMipmap() function is not reliable on all graphics cards
        unsigned int levels_count = generate_mipmaps(image);
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels_count));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
    }
    else
    {
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0));
    }
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

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
        nsvgDelete(image);
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
    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glGenTextures(1, &m_id));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_id));
#if ENABLE_COMPRESSED_TEXTURES
    if (GLEW_EXT_texture_compression_s3tc)
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#else
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#endif // ENABLE_COMPRESSED_TEXTURES
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
#if ENABLE_COMPRESSED_TEXTURES
            if (GLEW_EXT_texture_compression_s3tc)
                glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)lod_w, (GLsizei)lod_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
            else
                glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, (GLsizei)lod_w, (GLsizei)lod_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#else
            glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, (GLsizei)lod_w, (GLsizei)lod_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
#endif // ENABLE_COMPRESSED_TEXTURES
        }

        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
    }
    else
    {
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0));
    }
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    m_source = filename;

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    return true;
}

} // namespace GUI
} // namespace Slic3r
