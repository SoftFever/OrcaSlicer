#ifndef slic3r_GLTexture_hpp_
#define slic3r_GLTexture_hpp_

#include <string>

class wxImage;

namespace Slic3r {
namespace GUI {

    class GLTexture
    {
    protected:
        unsigned int m_id;
        int m_width;
        int m_height;
        std::string m_source;

    public:
        GLTexture();
        virtual ~GLTexture();

        bool load_from_file(const std::string& filename, bool generate_mipmaps);
        void reset();

        unsigned int get_id() const;
        int get_width() const;
        int get_height() const;

        const std::string& get_source() const;

        static void render_texture(unsigned int tex_id, float left, float right, float bottom, float top);

    protected:
        unsigned int _generate_mipmaps(wxImage& image);
    };

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLTexture_hpp_

