#ifndef slic3r_GUI_ThumbnailGenerator_hpp_
#define slic3r_GUI_ThumbnailGenerator_hpp_

#if ENABLE_THUMBNAIL_GENERATOR

#include "../libslic3r/GCode/ThumbnailData.hpp"

namespace Slic3r {
    class GLVolume;
    typedef std::vector<GLVolume*> GLVolumePtrs;
    namespace GUI {

    class ThumbnailGenerator
    {
        typedef std::vector<const GLVolume*> GLVolumeConstPtrs;

        ThumbnailData m_data;

    public:
        ThumbnailGenerator() { reset(); }

        void reset();

        void generate(const GLVolumePtrs& volumes, unsigned int w, unsigned int h, bool printable_only);

        const ThumbnailData& get_data() const { return m_data; }

        bool save_to_png_file(const std::string& filename);

    private:
        void render_and_store(const GLVolumePtrs& volumes, bool printable_only);
    };

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_THUMBNAIL_GENERATOR

#endif // slic3r_GUI_ThumbnailGenerator_hpp_

