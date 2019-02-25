#ifndef slic3r_GLTexture_hpp_
#define slic3r_GLTexture_hpp_

#include <string>

class wxImage;

namespace Slic3r {
namespace GUI {

    class GLTexture
    {
    public:
        struct UV
        {
            float u;
            float v;
        };

        struct Quad_UVs
        {
            UV left_bottom;
            UV right_bottom;
            UV right_top;
            UV left_top;
        };

        static Quad_UVs FullTextureUVs;

    protected:
        unsigned int m_id;
        int m_width;
        int m_height;
        std::string m_source;

    public:
        GLTexture();
        virtual ~GLTexture();

        bool load_from_file(const std::string& filename, bool use_mipmaps);
#if ENABLE_TEXTURES_FROM_SVG
        bool load_from_svg_file(const std::string& filename, bool use_mipmaps, unsigned int max_size_px);
#endif // ENABLE_TEXTURES_FROM_SVG
        void reset();

        unsigned int get_id() const { return m_id; }
        int get_width() const { return m_width; }
        int get_height() const { return m_height; }

        const std::string& get_source() const { return m_source; }

        static void render_texture(unsigned int tex_id, float left, float right, float bottom, float top);
        static void render_sub_texture(unsigned int tex_id, float left, float right, float bottom, float top, const Quad_UVs& uvs);

    protected:
        unsigned int generate_mipmaps(wxImage& image);
#if ENABLE_TEXTURES_FROM_SVG
    private:
        bool load_from_png(const std::string& filename, bool use_mipmaps);
        bool load_from_svg(const std::string& filename, bool use_mipmaps, unsigned int max_size_px);
#endif // ENABLE_TEXTURES_FROM_SVG
    };

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLTexture_hpp_

