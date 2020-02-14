#ifndef SLA_RASTER_HPP
#define SLA_RASTER_HPP

#include <ostream>
#include <memory>
#include <vector>
#include <array>
#include <utility>
#include <cstdint>

#include <libslic3r/ExPolygon.hpp>

namespace ClipperLib { struct Polygon; }

namespace Slic3r {
namespace sla {

/**
 * @brief Raster captures an anti-aliased monochrome canvas where vectorial
 * polygons can be rasterized. Fill color is always white and the background is
 * black. Contours are anti-aliased.
 *
 * It also supports saving the raster data into a standard output stream in raw
 * or PNG format.
 */
class Raster {
    class Impl;
    std::unique_ptr<Impl> m_impl;
public:

    // Raw byte buffer paired with its size. Suitable for compressed image data.
    class RawData
    {
    protected:
        std::vector<std::uint8_t> m_buffer;
        const Impl& get_internals(const Raster& raster);
    public:
        RawData() = default;
        RawData(std::vector<std::uint8_t>&& data): m_buffer(std::move(data)) {}
        virtual ~RawData();

        RawData(const RawData &) = delete;
        RawData &operator=(const RawData &) = delete;

        RawData(RawData &&) = default;
        RawData &operator=(RawData &&) = default;

        size_t size() const { return m_buffer.size(); }
        const uint8_t * data() const { return m_buffer.data(); }

        virtual RawData& serialize(const Raster &/*raster*/)  { return *this; }
        virtual std::string get_file_extension() const = 0;
    };

    /// Type that represents a resolution in pixels.
    struct Resolution {
        size_t width_px;
        size_t height_px;

        inline Resolution(size_t w = 0, size_t h = 0)
            : width_px(w), height_px(h)
        {}

        inline size_t pixels() const { return width_px * height_px; }
    };

    /// Types that represents the dimension of a pixel in millimeters.
    struct PixelDim {
        double w_mm;
        double h_mm;
        inline PixelDim(double px_width_mm = 0.0, double px_height_mm = 0.0):
            w_mm(px_width_mm), h_mm(px_height_mm) {}
    };

    enum Orientation { roLandscape, roPortrait };

    using TMirroring = std::array<bool, 2>;
    static const TMirroring NoMirror;
    static const TMirroring MirrorX;
    static const TMirroring MirrorY;
    static const TMirroring MirrorXY;

    struct Trafo {
        bool mirror_x = false, mirror_y = false, flipXY = false;
        coord_t origin_x = 0, origin_y = 0;

        // If gamma is zero, thresholding will be performed which disables AA.
        double gamma = 1.;

        // Portrait orientation will make sure the drawed polygons are rotated
        // by 90 degrees.
        Trafo(Orientation o = roLandscape, const TMirroring &mirror = NoMirror)
            // XY flipping implicitly does an X mirror
            : mirror_x(o == roPortrait ? !mirror[0] : mirror[0])
            , mirror_y(!mirror[1]) // Makes raster origin to be top left corner
            , flipXY(o == roPortrait)
        {}
    };

    Raster();
    Raster(const Resolution &r,
           const PixelDim &  pd,
           const Trafo &     tr = {});

    Raster(const Raster& cpy) = delete;
    Raster& operator=(const Raster& cpy) = delete;
    Raster(Raster&& m);
    Raster& operator=(Raster&&);
    ~Raster();

    /// Reallocated everything for the given resolution and pixel dimension.
    void reset(const Resolution& r,
               const PixelDim& pd,
               const Trafo &tr = {});

    /**
     * Release the allocated resources. Drawing in this state ends in
     * unspecified behavior.
     */
    void reset();

    /// Get the resolution of the raster.
    Resolution resolution() const;
    PixelDim   pixel_dimensions() const;

    /// Clear the raster with black color.
    void clear();

    /// Draw a polygon with holes.
    void draw(const ExPolygon& poly);
    void draw(const ClipperLib::Polygon& poly);

    uint8_t read_pixel(size_t w, size_t h) const;

    inline bool empty() const { return ! bool(m_impl); }

};

class PNGImage: public Raster::RawData {
public:
    PNGImage& serialize(const Raster &raster) override;
    std::string get_file_extension() const override { return "png"; }
};

class PPMImage: public Raster::RawData {
public:
    PPMImage& serialize(const Raster &raster) override;
    std::string get_file_extension() const override { return "ppm"; }
};

std::ostream& operator<<(std::ostream &stream, const Raster::RawData &bytes);

} // sla
} // Slic3r


#endif // SLARASTER_HPP
