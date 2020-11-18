#ifndef slic3r_FillRectilinear2_hpp_
#define slic3r_FillRectilinear2_hpp_

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class Surface;

class FillRectilinear2 : public Fill
{
public:
    virtual Fill* clone() const { return new FillRectilinear2(*this); };
    virtual ~FillRectilinear2() = default;
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
	bool fill_surface_by_lines(const Surface *surface, const FillParams &params, float angleBase, float pattern_shift, Polylines &polylines_out);
};

class FillMonotonic : public FillRectilinear2
{
public:
    virtual Fill* clone() const { return new FillMonotonic(*this); };
    virtual ~FillMonotonic() = default;
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);
	virtual bool no_sort() const { return true; }
};

class FillGrid2 : public FillRectilinear2
{
public:
    virtual Fill* clone() const { return new FillGrid2(*this); };
    virtual ~FillGrid2() = default;
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    virtual float _layer_angle(size_t idx) const { return 0.f; }
};

class FillTriangles : public FillRectilinear2
{
public:
    virtual Fill* clone() const { return new FillTriangles(*this); };
    virtual ~FillTriangles() = default;
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    virtual float _layer_angle(size_t idx) const { return 0.f; }
};

class FillStars : public FillRectilinear2
{
public:
    virtual Fill* clone() const { return new FillStars(*this); };
    virtual ~FillStars() = default;
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    virtual float _layer_angle(size_t idx) const { return 0.f; }
};

class FillCubic : public FillRectilinear2
{
public:
    virtual Fill* clone() const { return new FillCubic(*this); };
    virtual ~FillCubic() = default;
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    virtual float _layer_angle(size_t idx) const { return 0.f; }
};


}; // namespace Slic3r

#endif // slic3r_FillRectilinear2_hpp_
