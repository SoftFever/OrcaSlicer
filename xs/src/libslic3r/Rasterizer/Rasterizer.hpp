#ifndef RASTERIZER_HPP
#define RASTERIZER_HPP

#include <ostream>
#include <Polygon.hpp>

namespace Slic3r {

class Raster {
    class Impl;
    std::unique_ptr<Impl> impl_;
public:

    enum class Compression {
        RAW,
        PNG
    };

    struct Resolution {
        unsigned width_px;
        unsigned height_px;
        inline Resolution(unsigned w, unsigned h): width_px(w), height_px(h) {}
        inline unsigned pixels() const /*noexcept*/ {
            return width_px * height_px;
        }
    };

    struct PixelDim {
        double w_mm;
        double h_mm;
        inline PixelDim(double px_width_mm, double px_height_mm ):
            w_mm(px_width_mm), h_mm(px_height_mm) {}
    };

    explicit Raster(const Resolution& r, const PixelDim& pd );
    ~Raster();
    Raster(const Raster& cpy);
    Raster(Raster&& m);

    void clear();

    void draw(const Polygon& poly);

    void save(std::ostream& stream, Compression comp = Compression::RAW);
};

}
#endif // RASTERIZER_HPP
