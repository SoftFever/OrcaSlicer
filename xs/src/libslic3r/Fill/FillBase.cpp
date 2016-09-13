#include <stdio.h>

#include "../ClipperUtils.hpp"
#include "../Surface.hpp"

#include "FillBase.hpp"
#include "FillConcentric.hpp"
#include "FillHoneycomb.hpp"
#include "Fill3DHoneycomb.hpp"
#include "FillPlanePath.hpp"
#include "FillRectilinear.hpp"
#include "FillRectilinear2.hpp"

namespace Slic3r {

Fill* Fill::new_from_type(const std::string &type)
{
	if (type == "concentric")
		return new FillConcentric();
	if (type == "honeycomb")
		return new FillHoneycomb();
	if (type == "3dhoneycomb")
		return new Fill3DHoneycomb();
	if (type == "rectilinear")
//		return new FillRectilinear();
        return new FillRectilinear2();
	if (type == "line")
		return new FillLine();
	if (type == "grid")
//		return new FillGrid();
        return new FillGrid2();
	if (type == "archimedeanchords")
		return new FillArchimedeanChords();
	if (type == "hilbertcurve")
		return new FillHilbertCurve();
	if (type == "octagramspiral")
		return new FillOctagramSpiral();
	CONFESS("unknown type");
	return NULL;
}

Polylines Fill::fill_surface(const Surface *surface, const FillParams &params)
{
    // Perform offset.
    Slic3r::ExPolygons expp;
    offset(surface->expolygon, &expp, -0.5*scale_(this->spacing));
    // Create the infills for each of the regions.
    Polylines polylines_out;
    for (size_t i = 0; i < expp.size(); ++ i)
        _fill_surface_single(
            params,
            surface->thickness_layers,
            _infill_direction(surface),
            expp[i],
            polylines_out);
    return polylines_out;
}

// Calculate a new spacing to fill width with possibly integer number of lines,
// the first and last line being centered at the interval ends.
//FIXME Vojtech: This 
// This function possibly increases the spacing, never decreases, 
// and for a narrow width the increase in spacing may become severe!
coord_t Fill::_adjust_solid_spacing(const coord_t width, const coord_t distance)
{
    coord_t number_of_intervals = coord_t(coordf_t(width) / coordf_t(distance));
    return (number_of_intervals == 0) ? 
        distance : 
        (width / number_of_intervals);
}

// Returns orientation of the infill and the reference point of the infill pattern.
// For a normal print, the reference point is the center of a bounding box of the STL.
std::pair<float, Point> Fill::_infill_direction(const Surface *surface) const
{
    // set infill angle
    float out_angle = this->angle;

	if (out_angle == FLT_MAX) {
		//FIXME Vojtech: Add a warning?
        printf("Using undefined infill angle\n");
        out_angle = 0.f;
    }

    // Bounding box is the bounding box of a perl object Slic3r::Print::Object (c++ object Slic3r::PrintObject)
    // The bounding box is only undefined in unit tests.
    Point out_shift = empty(this->bounding_box) ? 
    	surface->expolygon.contour.bounding_box().center() : 
        this->bounding_box.center();

#if 0
    if (empty(this->bounding_box)) {
        printf("Fill::_infill_direction: empty bounding box!");
    } else {
        printf("Fill::_infill_direction: reference point %d, %d\n", out_shift.x, out_shift.y);
    }
#endif

    if (surface->bridge_angle >= 0) {
	    // use bridge angle
		//FIXME Vojtech: Add a debugf?
        // Slic3r::debugf "Filling bridge with angle %d\n", rad2deg($surface->bridge_angle);
#ifdef SLIC3R_DEBUG
        printf("Filling bridge with angle %f\n", surface->bridge_angle);
#endif /* SLIC3R_DEBUG */
        out_angle = surface->bridge_angle;
    } else if (this->layer_id != size_t(-1)) {
        // alternate fill direction
        out_angle += this->_layer_angle(this->layer_id / surface->thickness_layers);
    } else {
//    	printf("Layer_ID undefined!\n");
    }

    out_angle += float(M_PI/2.);
    return std::pair<float, Point>(out_angle, out_shift);
}

} // namespace Slic3r
