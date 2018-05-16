#ifndef RASTERIZER_HPP
#define RASTERIZER_HPP

#include <Polygon.hpp>

namespace Slic3r {

class Raster {
    class Impl;
    std::unique_ptr<Impl> impl_;
public:

    struct Resolution {
        unsigned width_px;
        unsigned height_px;
        inline Resolution(unsigned w, unsigned h): width_px(w), height_px(h) {}
    };

    struct PixelDim {
        double w_mm;
        double h_mm;
        inline PixelDim(double px_width_mm, double px_height_mm ):
            w_mm(px_width_mm), h_mm(px_height_mm) {}
    };

    inline explicit Raster(const Resolution& r, const PixelDim& pd );
    ~Raster();
    Raster(const Raster& cpy);
    Raster(Raster&& m);

    void clear();

    void draw(const Polygon& poly);

    void finish();

private:
    Resolution resolution_;
    PixelDim pxdim_;
};

}
#endif // RASTERIZER_HPP
