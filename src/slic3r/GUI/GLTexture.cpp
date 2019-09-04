#include "libslic3r/libslic3r.h"
#include "GLTexture.hpp"

#include "3DScene.hpp"

#include <GL/glew.h>

#include <wx/image.h>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <vector>
#include <algorithm>
#include <thread>

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt/stb_dxt.h"

#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

#include "libslic3r/Utils.hpp"

namespace Slic3r {
namespace GUI {

void GLTexture::Compressor::reset()
{
	if (m_thread.joinable()) {
        m_abort_compressing = true;
		m_thread.join();
	    m_levels.clear();
	    m_num_levels_compressed = 0;
	    m_abort_compressing = false;
	}
	assert(m_levels.empty());
	assert(m_abort_compressing == false);
	assert(m_num_levels_compressed == 0);
}

void GLTexture::Compressor::start_compressing()
{
	// The worker thread should be stopped already.
	assert(! m_thread.joinable());
	assert(! m_levels.empty());
	assert(m_abort_compressing == false);
	assert(m_num_levels_compressed == 0);
	if (! m_levels.empty()) {
		std::thread thrd(&GLTexture::Compressor::compress, this);
    	m_thread = std::move(thrd);
    }
}

bool GLTexture::Compressor::unsent_compressed_data_available() const
{
	if (m_levels.empty())
		return false;
	// Querying the atomic m_num_levels_compressed value synchronizes processor caches, so that the data of m_levels modified by the worker thread are accessible to the calling thread.
	unsigned int num_compressed = m_num_levels_compressed;
	for (unsigned int i = 0; i < num_compressed; ++ i)
        if (! m_levels[i].sent_to_gpu && ! m_levels[i].compressed_data.empty())
            return true;
    return false;
}

void GLTexture::Compressor::send_compressed_data_to_gpu()
{
    // this method should be called inside the main thread of Slicer or a new OpenGL context (sharing resources) would be needed
	if (m_levels.empty())
		return;

    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_texture.m_id));
	// Querying the atomic m_num_levels_compressed value synchronizes processor caches, so that the dat of m_levels modified by the worker thread are accessible to the calling thread.
	int num_compressed = (int)m_num_levels_compressed;
    for (int i = 0; i < num_compressed; ++ i)
    {
        Level& level = m_levels[i];
        if (! level.sent_to_gpu && ! level.compressed_data.empty())
        {
            glsafe(::glCompressedTexSubImage2D(GL_TEXTURE_2D, (GLint)i, 0, 0, (GLsizei)level.w, (GLsizei)level.h, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)level.compressed_data.size(), (const GLvoid*)level.compressed_data.data()));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, i));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (i > 0) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR));
            level.sent_to_gpu = true;
            // we are done with the compressed data, we can discard it
            level.compressed_data.clear();
        }
    }
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    if (num_compressed == (int)m_levels.size())
        // Finalize the worker thread, close it.
    	this->reset();
}

void GLTexture::Compressor::compress()
{
    // reference: https://github.com/Cyan4973/RygsDXTc

    assert(m_num_levels_compressed == 0);
    assert(m_abort_compressing == false);

    for (Level& level : m_levels)
    {
        if (m_abort_compressing)
            break;

        // stb_dxt library, despite claiming that the needed size of the destination buffer is equal to (source buffer size)/4,
        // crashes if doing so, so we start with twice the required size
        level.compressed_data = std::vector<unsigned char>(level.w * level.h * 2, 0);
        int compressed_size = 0;
        rygCompress(level.compressed_data.data(), level.src_data.data(), level.w, level.h, 1, compressed_size);
        level.compressed_data.resize(compressed_size);

        // we are done with the source data, we can discard it
        level.src_data.clear();
        ++ m_num_levels_compressed;
    }
}

GLTexture::Quad_UVs GLTexture::FullTextureUVs = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };

GLTexture::GLTexture()
    : m_id(0)
    , m_width(0)
    , m_height(0)
    , m_source("")
    , m_compressor(*this)
{
}

GLTexture::~GLTexture()
{
    reset();
}

bool GLTexture::load_from_file(const std::string& filename, bool use_mipmaps, ECompressionType compression_type, bool apply_anisotropy)
{
    reset();

    if (!boost::filesystem::exists(filename))
        return false;

    if (boost::algorithm::iends_with(filename, ".png"))
        return load_from_png(filename, use_mipmaps, compression_type, apply_anisotropy);
    else
        return false;
}

bool GLTexture::load_from_svg_file(const std::string& filename, bool use_mipmaps, bool compress, bool apply_anisotropy, unsigned int max_size_px)
{
    reset();

    if (!boost::filesystem::exists(filename))
        return false;

    if (boost::algorithm::iends_with(filename, ".svg"))
        return load_from_svg(filename, use_mipmaps, compress, apply_anisotropy, max_size_px);
    else
        return false;
}

bool GLTexture::load_from_svg_files_as_sprites_array(const std::vector<std::string>& filenames, const std::vector<std::pair<int, bool>>& states, unsigned int sprite_size_px, bool compress)
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
    if (compress && GLEW_EXT_texture_compression_s3tc)
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
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
    m_compressor.reset();
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

bool GLTexture::load_from_png(const std::string& filename, bool use_mipmaps, ECompressionType compression_type, bool apply_anisotropy)
{
    bool compression_enabled = (compression_type != None) && GLEW_EXT_texture_compression_s3tc;

    // Load a PNG with an alpha channel.
    wxImage image;
    if (!image.LoadFile(wxString::FromUTF8(filename.c_str()), wxBITMAP_TYPE_PNG))
    {
        reset();
        return false;
    }

    m_width = image.GetWidth();
    m_height = image.GetHeight();

    bool requires_rescale = false;

    if (compression_enabled && (compression_type == MultiThreaded))
    {
        // the stb_dxt compression library seems to like only texture sizes which are a multiple of 4
        int width_rem = m_width % 4;
        int height_rem = m_height % 4;

        if (width_rem != 0)
        {
            m_width += (4 - width_rem);
            requires_rescale = true;
        }

        if (height_rem != 0)
        {
            m_height += (4 - height_rem);
            requires_rescale = true;
        }
    }

    if (requires_rescale)
        image = image.ResampleBicubic(m_width, m_height);

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

    if (apply_anisotropy)
    {
        GLfloat max_anisotropy = GLCanvas3DManager::get_gl_info().get_max_anisotropy();
        if (max_anisotropy > 1.0f)
            glsafe(::glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropy));
    }

    if (compression_enabled)
    {
        if (compression_type == SingleThreaded)
            glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
        else
        {
            // initializes the texture on GPU 
            glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
            // and send the uncompressed data to the compressor
            m_compressor.add_level((unsigned int)m_width, (unsigned int)m_height, data);
        }
    }
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));

    if (use_mipmaps)
    {
        // we manually generate mipmaps because glGenerateMipmap() function is not reliable on all graphics cards
        int lod_w = m_width;
        int lod_h = m_height;
        GLint level = 0;
        // we do not need to generate all levels down to 1x1
        while ((lod_w > 16) || (lod_h > 16))
        {
            ++level;

            lod_w = std::max(lod_w / 2, 1);
            lod_h = std::max(lod_h / 2, 1);
            n_pixels = lod_w * lod_h;

            image = image.ResampleBicubic(lod_w, lod_h);

            data.resize(n_pixels * 4);

            img_rgb = image.GetData();
            img_alpha = image.GetAlpha();

            for (int i = 0; i < n_pixels; ++i)
            {
                int data_id = i * 4;
                int img_id = i * 3;
                data[data_id + 0] = img_rgb[img_id + 0];
                data[data_id + 1] = img_rgb[img_id + 1];
                data[data_id + 2] = img_rgb[img_id + 2];
                data[data_id + 3] = (img_alpha != nullptr) ? img_alpha[i] : 255;
            }

            if (compression_enabled)
            {
                if (compression_type == SingleThreaded)
                    glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
                else
                {
                    // initializes the texture on GPU 
                    glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)lod_w, (GLsizei)lod_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
                    // and send the uncompressed data to the compressor
                    m_compressor.add_level((unsigned int)lod_w, (unsigned int)lod_h, data);
                }
            }
            else
                glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, (GLsizei)lod_w, (GLsizei)lod_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
        }

        if (!compression_enabled)
        {
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
        }
    }
    else
    {
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0));
    }

    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    m_source = filename;

    if (compression_enabled && (compression_type == MultiThreaded))
        // start asynchronous compression
        m_compressor.start_compressing();

    return true;
}

bool GLTexture::load_from_svg(const std::string& filename, bool use_mipmaps, bool compress, bool apply_anisotropy, unsigned int max_size_px)
{
    bool compression_enabled = compress && GLEW_EXT_texture_compression_s3tc;

    NSVGimage* image = nsvgParseFromFile(filename.c_str(), "px", 96.0f);
    if (image == nullptr)
    {
        reset();
        return false;
    }

    float scale = (float)max_size_px / std::max(image->width, image->height);

    m_width = (int)(scale * image->width);
    m_height = (int)(scale * image->height);

    if (compression_enabled)
    {
        // the stb_dxt compression library seems to like only texture sizes which are a multiple of 4
        int width_rem = m_width % 4;
        int height_rem = m_height % 4;

        if (width_rem != 0)
            m_width += (4 - width_rem);

        if (height_rem != 0)
            m_height += (4 - height_rem);
    }

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

    if (apply_anisotropy)
    {
        GLfloat max_anisotropy = GLCanvas3DManager::get_gl_info().get_max_anisotropy();
        if (max_anisotropy > 1.0f)
            glsafe(::glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropy));
    }

    if (compression_enabled)
    {
        // initializes the texture on GPU 
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
        // and send the uncompressed data to the compressor
        m_compressor.add_level((unsigned int)m_width, (unsigned int)m_height, data);
    }
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));

    if (use_mipmaps)
    {
        // we manually generate mipmaps because glGenerateMipmap() function is not reliable on all graphics cards
        int lod_w = m_width;
        int lod_h = m_height;
        GLint level = 0;
        // we do not need to generate all levels down to 1x1
        while ((lod_w > 16) || (lod_h > 16))
        {
            ++level;

            lod_w = std::max(lod_w / 2, 1);
            lod_h = std::max(lod_h / 2, 1);
            scale /= 2.0f;

            data.resize(lod_w * lod_h * 4);

            nsvgRasterize(rast, image, 0, 0, scale, data.data(), lod_w, lod_h, lod_w * 4);
            if (compression_enabled)
            {
                // initializes the texture on GPU 
                glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, (GLsizei)lod_w, (GLsizei)lod_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
                // and send the uncompressed data to the compressor
                m_compressor.add_level((unsigned int)lod_w, (unsigned int)lod_h, data);
            }
            else
                glsafe(::glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, (GLsizei)lod_w, (GLsizei)lod_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data()));
        }

        if (!compression_enabled)
        {
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
        }
    }
    else
    {
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0));
    }

    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    m_source = filename;

    if (compression_enabled)
        // start asynchronous compression
        m_compressor.start_compressing();

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    return true;
}

} // namespace GUI
} // namespace Slic3r
