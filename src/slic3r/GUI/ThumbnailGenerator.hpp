#ifndef slic3r_GUI_ThumbnailGenerator_hpp_
#define slic3r_GUI_ThumbnailGenerator_hpp_

#if ENABLE_THUMBNAIL_GENERATOR

#include "../libslic3r/GCode/ThumbnailData.hpp"

#include <array>

namespace Slic3r {
    class GLVolume;
namespace GUI {
    class GLCanvas3D;

    class ThumbnailGenerator
    {
        ThumbnailData m_data;

    public:
        ThumbnailGenerator() { reset(); }

        void reset();

        bool render_to_png_file(const GLCanvas3D& canvas, const std::string& filename, unsigned int w, unsigned int h, bool printable_only);

    private:
        void render(const GLCanvas3D& canvas, bool printable_only);
        void render_objects(const std::vector<const GLVolume*>& volumes) const;
    };

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_THUMBNAIL_GENERATOR

#endif // slic3r_GUI_ThumbnailGenerator_hpp_

