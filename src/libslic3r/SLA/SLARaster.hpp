#ifndef SLARASTER_HPP
#define SLARASTER_HPP

#include <ostream>
#include <memory>
#include <vector>
#include <array>
#include <utility>
#include <cstdint>

namespace ClipperLib { struct Polygon; }

namespace Slic3r { 

class ExPolygon;

namespace sla {

// Raw byte buffer paired with its size. Suitable for compressed PNG data.
class RawBytes {

    std::vector<std::uint8_t> m_buffer;
public:

    RawBytes() = default;
    RawBytes(std::vector<std::uint8_t>&& data): m_buffer(std::move(data)) {}
    
    size_t size() const { return m_buffer.size(); }
    const uint8_t * data() { return m_buffer.data(); }
    
    RawBytes(const RawBytes&) = delete;
    RawBytes& operator=(const RawBytes&) = delete;

    // /////////////////////////////////////////////////////////////////////////
    // FIXME: the following is needed for MSVC2013 compatibility
    // /////////////////////////////////////////////////////////////////////////

    // RawBytes(RawBytes&&) = default;
    // RawBytes& operator=(RawBytes&&) = default;

    RawBytes(RawBytes&& mv) : m_buffer(std::move(mv.m_buffer)) {}
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
    enum class Format {
        RAW,    //!> Uncompressed pixel data
        PNG     //!> PNG compression
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

    /// Constructor taking the resolution and the pixel dimension.
    template <class...Args> Raster(Args...args) { 
        reset(std::forward<Args>(args)...); 
    }
    
    Raster(const Raster& cpy) = delete;
    Raster& operator=(const Raster& cpy) = delete;
    Raster(Raster&& m);
    Raster& operator=(Raster&&);
    ~Raster();

    /// Reallocated everything for the given resolution and pixel dimension.
    /// The third parameter is either the X, Y mirroring or a supported format 
    /// for which the correct mirroring will be configured.
    void reset(const Resolution&, 
               const PixelDim&, 
               const std::array<bool, 2>& mirror, 
               double gamma = 1.0);
    
    void reset(const Resolution& r, 
               const PixelDim& pd, 
               Format o, 
               double gamma = 1.0);
    
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

    // Saving the raster: 
    // It is possible to override the format given in the constructor but
    // be aware that the mirroring will not be modified.
    
    /// Save the raster on the specified stream.
    void save(std::ostream& stream, Format);
    void save(std::ostream& stream);

    /// Save into a continuous byte stream which is returned.
    RawBytes save(Format fmt);
    RawBytes save();
};

// This prevents the duplicate default constructor warning on MSVC2013
template<> Raster::Raster();


} // sla
} // Slic3r

#endif // SLARASTER_HPP
