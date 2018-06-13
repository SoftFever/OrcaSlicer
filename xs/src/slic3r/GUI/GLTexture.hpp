#ifndef slic3r_GLTexture_hpp_
#define slic3r_GLTexture_hpp_

#include <string>

namespace Slic3r {
namespace GUI {

    struct GLTexture
    {
    private:
        unsigned int m_id;
        int m_width;
        int m_height;
        std::string m_source;

    public:
        GLTexture();
        ~GLTexture();

        bool load_from_file(const std::string& filename);
        void reset();

        unsigned int get_id() const;
        int get_width() const;
        int get_height() const;
        const std::string& get_source() const;

        static void render_texture(unsigned int tex_id, float left, float right, float bottom, float top);
    };

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLTexture_hpp_

