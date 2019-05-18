#ifndef RASTERIZER_HPP
#define RASTERIZER_HPP

#include <ostream>
#include <memory>
#include <vector>
#include <cstdint>

namespace ClipperLib { struct Polygon; }

namespace Slic3r {

class ExPolygon;

// Raw byte buffer paired with its size. Suitable for compressed PNG data.
class RawBytes {

    std::vector<std::uint8_t> m_buffer;
public:

    RawBytes() = default;
    RawBytes(std::vector<std::uint8_t>&& data): m_buffer(std::move(data)) {}
    
    size_t size() const { return m_buffer.size(); }
    const uint8_t * data() { return m_buffer.data(); }

    // /////////////////////////////////////////////////////////////////////////
    // FIXME: the following is needed for MSVC2013 compatibility
    // /////////////////////////////////////////////////////////////////////////

    RawBytes(const RawBytes&) = delete;
    RawBytes(RawBytes&& mv) : m_buffer(std::move(mv.m_buffer)) {}

    RawBytes& operator=(const RawBytes&) = delete;
    RawBytes& operator=(RawBytes&& mv) {
        m_buffer = std::move(mv.m_buffer);
        return *this;
    }

    // /////////////////////////////////////////////////////////////////////////
};

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

    /// Supported compression types
    enum class Compression {
        RAW,    //!> Uncompressed pixel data
        PNG     //!> PNG compression
    };

    /// The Rasterizer expects the input polygons to have their coordinate
    /// system origin in the bottom left corner. If the raster is then
    /// configured with the TOP_LEFT origin parameter (in the constructor) than
    /// it will flip the Y axis in output to maintain the correct orientation.
    /// This is the default case with PNG images. They have the origin in the
    /// top left corner. Without the flipping, the image would be upside down
    /// with the scaled (clipper) coordinate system of the input polygons.
    enum class Origin {
        TOP_LEFT,
        BOTTOM_LEFT
    };

    /// Type that represents a resolution in pixels.
    struct Resolution {
        unsigned width_px;
        unsigned height_px;

        inline Resolution(unsigned w = 0, unsigned h = 0):
            width_px(w), height_px(h) {}

        inline unsigned pixels() const /*noexcept*/ {
            return width_px * height_px;
        }
    };

    /// Types that represents the dimension of a pixel in millimeters.
    struct PixelDim {
        double w_mm;
        double h_mm;
        inline PixelDim(double px_width_mm = 0.0, double px_height_mm = 0.0):
            w_mm(px_width_mm), h_mm(px_height_mm) {}
    };

    /// Constructor taking the resolution and the pixel dimension.
    Raster(const Resolution& r,  const PixelDim& pd, 
           Origin o = Origin::BOTTOM_LEFT, double gamma = 1.0);
    
    Raster();
    Raster(const Raster& cpy) = delete;
    Raster& operator=(const Raster& cpy) = delete;
    Raster(Raster&& m);
    ~Raster();

    /// Reallocated everything for the given resolution and pixel dimension.
    void reset(const Resolution& r, const PixelDim& pd, double gamma = 1.0);
    void reset(const Resolution& r, const PixelDim& pd, Origin o, double gamma);

    /**
     * Release the allocated resources. Drawing in this state ends in
     * unspecified behavior.
     */
    void reset();

    /// Get the resolution of the raster.
    Resolution resolution() const;

    /// Clear the raster with black color.
    void clear();

    /// Draw a polygon with holes.
    void draw(const ExPolygon& poly);
    void draw(const ClipperLib::Polygon& poly);

    /// Save the raster on the specified stream.
    void save(std::ostream& stream, Compression comp = Compression::RAW);

    RawBytes save(Compression comp = Compression::RAW);
};

}
#endif // RASTERIZER_HPP
