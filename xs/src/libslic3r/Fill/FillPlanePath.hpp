#ifndef slic3r_FillPlanePath_hpp_
#define slic3r_FillPlanePath_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

// The original Perl code used path generators from Math::PlanePath library:
// http://user42.tuxfamily.org/math-planepath/
// http://user42.tuxfamily.org/math-planepath/gallery.html

class FillPlanePath : public FillWithDirection
{
public:
    virtual ~FillPlanePath() {}
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
    virtual float _layer_angle(size_t idx) const { return 0.f; }
    virtual bool  _centered() const = 0;
    virtual Pointfs _generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y) = 0;
};

class FillArchimedeanChords : public FillPlanePath
{
public:
    virtual ~FillArchimedeanChords() {}

protected:
    virtual bool  _centered() const { return true; }
    virtual Pointfs _generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y);
};

class FillHilbertCurve : public FillPlanePath
{
public:
    virtual ~FillHilbertCurve() {}

protected:
    virtual bool  _centered() const { return false; }
    virtual Pointfs _generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y);
};

class FillOctagramSpiral : public FillPlanePath
{
public:
    virtual ~FillOctagramSpiral() {}

protected:
    virtual bool  _centered() const { return true; }
    virtual Pointfs _generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y);
};

} // namespace Slic3r

#endif // slic3r_FillPlanePath_hpp_
