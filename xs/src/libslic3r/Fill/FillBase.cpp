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
		return new FillGrid();
	if (type == "archimedeanchords")
		return new FillArchimedeanChords();
	if (type == "hilbertcurve")
		return new FillHilbertCurve();
	if (type == "octagramspiral")
		return new FillOctagramSpiral();
	CONFESS("unknown type");
	return NULL;
}

coord_t Fill::adjust_solid_spacing(const coord_t width, const coord_t distance)
{
    coord_t number_of_lines = coord_t(coordf_t(width) / coordf_t(distance)) + 1;
    coord_t extra_space     = width % distance;
    return (number_of_lines <= 1) ? 
    	distance : 
    	distance + extra_space / (number_of_lines - 1);
}

std::pair<float, Point> FillWithDirection::infill_direction(const Surface *surface) const
{
    // set infill angle
    float out_angle = this->angle;

	if (out_angle == FLT_MAX) {
		//FIXME Vojtech: Add a warning?
        printf("Using undefined infill angle\n");
        out_angle = 0.f;
    }

    Point out_shift = empty(this->bounding_box) ? 
    	surface->expolygon.contour.bounding_box().center() : 
        this->bounding_box.center();

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
    	printf("Layer_ID undefined!\n");
    }

    out_angle += float(M_PI/2.);
    return std::pair<float, Point>(out_angle, out_shift);
}

} // namespace Slic3r
