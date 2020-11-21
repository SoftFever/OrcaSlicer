#ifndef slic3r_FillRectilinear_hpp_
#define slic3r_FillRectilinear_hpp_

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class Surface;

class FillRectilinear : public Fill
{
public:
    Fill* clone() const override { return new FillRectilinear(*this); };
    ~FillRectilinear() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;

protected:
    // Fill by single directional lines, interconnect the lines along perimeters.
	bool fill_surface_by_lines(const Surface *surface, const FillParams &params, float angleBase, float pattern_shift, Polylines &polylines_out);


    // Fill by multiple sweeps of differing directions.
    struct SweepParams {
        float angle_base;
        float pattern_shift;
    };
    bool fill_surface_by_multilines(const Surface *surface, FillParams params, const std::initializer_list<SweepParams> &sweep_params, Polylines &polylines_out);
};

class FillMonotonic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillMonotonic(*this); };
    ~FillMonotonic() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
	bool no_sort() const override { return true; }
};

class FillGrid : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillGrid(*this); };
    ~FillGrid() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillTriangles : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillTriangles(*this); };
    ~FillTriangles() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillStars : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillStars(*this); };
    ~FillStars() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillCubic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillCubic(*this); };
    ~FillCubic() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};


}; // namespace Slic3r

#endif // slic3r_FillRectilinear_hpp_
