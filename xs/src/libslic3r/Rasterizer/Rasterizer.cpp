#include "Rasterizer.hpp"

#include <png.h>


namespace Slic3r {

class Raster::Impl {

};

Raster::Raster(const Raster::Resolution &r, const Raster::PixelDim &pd):
    impl_(new Impl), resolution_(r), pxdim_(pd) {}

Raster::~Raster() {}

Raster::Raster(const Raster &cpy): resolution_(cpy.resolution_),
    pxdim_(cpy.pxdim_) {
    *impl_ = *(cpy.impl_);
}

Raster::Raster(Raster &&m):
    impl_(std::move(m.impl_)), resolution_(m.resolution_), pxdim_(m.pxdim_) {}

void Raster::clear()
{

}

void Raster::draw(const Polygon &poly)
{
    png_image ifo;
}

void Raster::finish()
{

}

}
