#ifndef slic3r_FillPlanePath_hpp_
#define slic3r_FillPlanePath_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

// The original Perl code used path generators from Math::PlanePath library:
// http://user42.tuxfamily.org/math-planepath/
// http://user42.tuxfamily.org/math-planepath/gallery.html

class FillPlanePath : public Fill
{
public:
    ~FillPlanePath() override = default;

protected:
    void _fill_surface_single(
        const FillParams                &params, 
        unsigned int                     thickness_layers,
        const std::pair<float, Point>   &direction, 
        ExPolygon                        expolygon,
        Polylines                       &polylines_out) override;

    float _layer_angle(size_t idx) const override { return 0.f; }
    virtual bool  _centered() const = 0;
    virtual Pointfs _generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution) = 0;
};

class FillArchimedeanChords : public FillPlanePath
{
public:
    Fill* clone() const override { return new FillArchimedeanChords(*this); };
    ~FillArchimedeanChords() override = default;

protected:
    bool  _centered() const override { return true; }
    Pointfs _generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution) override;
};

class FillHilbertCurve : public FillPlanePath
{
public:
    Fill* clone() const override { return new FillHilbertCurve(*this); };
    ~FillHilbertCurve() override = default;

protected:
    bool  _centered() const override { return false; }
    Pointfs _generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution) override;
};

class FillOctagramSpiral : public FillPlanePath
{
public:
    Fill* clone() const override { return new FillOctagramSpiral(*this); };
    ~FillOctagramSpiral() override = default;

protected:
    bool  _centered() const override { return true; }
    Pointfs _generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution) override;
};

} // namespace Slic3r

#endif // slic3r_FillPlanePath_hpp_
