#ifndef slic3r_GLTexture_hpp_
#define slic3r_GLTexture_hpp_

#include <string>
#include <vector>

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

#if ENABLE_COMPRESSED_TEXTURES
        bool load_from_file(const std::string& filename, bool use_mipmaps, bool compress);
        bool load_from_svg_file(const std::string& filename, bool use_mipmaps, bool compress, bool apply_anisotropy, unsigned int max_size_px);
#else
        bool load_from_file(const std::string& filename, bool use_mipmaps);
        bool load_from_svg_file(const std::string& filename, bool use_mipmaps, unsigned int max_size_px);
#endif // ENABLE_COMPRESSED_TEXTURES
        // meanings of states: (std::pair<int, bool>)
        // first field (int):
        // 0 -> no changes
        // 1 -> use white only color variant
        // 2 -> use gray only color variant
        // second field (bool):
        // false -> no changes
        // true -> add background color
#if ENABLE_COMPRESSED_TEXTURES
        bool load_from_svg_files_as_sprites_array(const std::vector<std::string>& filenames, const std::vector<std::pair<int, bool>>& states, unsigned int sprite_size_px, bool compress);
#else
        bool load_from_svg_files_as_sprites_array(const std::vector<std::string>& filenames, const std::vector<std::pair<int, bool>>& states, unsigned int sprite_size_px);
#endif // ENABLE_COMPRESSED_TEXTURES
        void reset();

        unsigned int get_id() const { return m_id; }
        int get_width() const { return m_width; }
        int get_height() const { return m_height; }

        const std::string& get_source() const { return m_source; }

        static void render_texture(unsigned int tex_id, float left, float right, float bottom, float top);
        static void render_sub_texture(unsigned int tex_id, float left, float right, float bottom, float top, const Quad_UVs& uvs);

    protected:
#if ENABLE_COMPRESSED_TEXTURES
        unsigned int generate_mipmaps(wxImage& image, bool compress);
#else
        unsigned int generate_mipmaps(wxImage& image);
#endif // ENABLE_COMPRESSED_TEXTURES

    private:
#if ENABLE_COMPRESSED_TEXTURES
        bool load_from_png(const std::string& filename, bool use_mipmaps, bool compress);
        bool load_from_svg(const std::string& filename, bool use_mipmaps, bool compress, bool apply_anisotropy, unsigned int max_size_px);
#else
        bool load_from_png(const std::string& filename, bool use_mipmaps);
        bool load_from_svg(const std::string& filename, bool use_mipmaps, unsigned int max_size_px);
#endif // ENABLE_COMPRESSED_TEXTURES
    };

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLTexture_hpp_

