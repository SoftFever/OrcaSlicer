#ifndef RASTERIZER_HPP
#define RASTERIZER_HPP

#include <ostream>
#include <memory>

namespace Slic3r {

class ExPolygon;

/**
 * @brief Raster captures an antialiased monochrome canvas where vectorial
 * polygons can be rasterized. Fill color is always white and the background is
 * black. Countours are antialiased.
 *
 * It also supports saving the raster data into a standard output stream in raw
 * or PNG format.
 */
class Raster {
    class Impl;
    std::unique_ptr<Impl> impl_;
public:

    /// Supported compression types
    enum class Compression {
        RAW,    //!> Uncompressed pixel data
        PNG     //!> PNG compression
    };

    /// Type that represents a resolution in pixels.
    struct Resolution {
        unsigned width_px;
        unsigned height_px;
        inline Resolution(unsigned w, unsigned h): width_px(w), height_px(h) {}
        inline unsigned pixels() const /*noexcept*/ {
            return width_px * height_px;
        }
    };

    /// Types that represents the dimension of a pixel in millimeters.
    struct PixelDim {
        double w_mm;
        double h_mm;
        inline PixelDim(double px_width_mm, double px_height_mm ):
            w_mm(px_width_mm), h_mm(px_height_mm) {}
    };

    /// Constructor taking the resolution and the pixel dimension.
    explicit Raster(const Resolution& r, const PixelDim& pd );
    Raster();
    Raster(const Raster& cpy);
    Raster(Raster&& m);
    ~Raster();

    /// Reallocated everything for the given resolution and pixel dimension.
    void reset(const Resolution& r, const PixelDim& pd);

    /**
     * Release the allocated resources. Drawing in this state ends in
     * unspecified behaviour.
     */
    void reset();

    /// Get the resolution of the raster.
    Resolution resolution() const;

    /// Clear the raster with black color.
    void clear();

    /// Draw a polygon with holes.
    void draw(const ExPolygon& poly);

    /// Save the raster on the specified stream.
    void save(std::ostream& stream, Compression comp = Compression::RAW);
};

}
#endif // RASTERIZER_HPP
