#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"

#include "FillConcentric.hpp"

namespace Slic3r {

Polylines FillConcentric::fill_surface(const Surface *surface, const FillParams &params)
{
    // no rotation is supported for this infill pattern
    ExPolygon expolygon = surface->expolygon;
    BoundingBox bounding_box = expolygon.contour.bounding_box();
    
    coord_t min_spacing = scale_(this->spacing);
    coord_t distance = coord_t(min_spacing / params.density);
    
    if (params.density > 0.9999f && !params.dont_adjust) {
        distance = this->adjust_solid_spacing(bounding_box.size().x, distance);
        this->spacing = unscale(distance);
    }

    Polygons loops = (Polygons)expolygon;
    Polygons last  = loops;
    while (! last.empty()) {
        last = offset2(last, -(distance + min_spacing/2), +min_spacing/2);
        loops.insert(loops.end(), last.begin(), last.end());
    }

    // generate paths from the outermost to the innermost, to avoid
    // adhesion problems of the first central tiny loops
    union_pt_chained(loops, &loops, false);
    
    // split paths using a nearest neighbor search
    Polylines paths;
    Point last_pos(0, 0);
    for (Polygons::const_iterator it_loop = loops.begin(); it_loop != loops.end(); ++ it_loop) {
        paths.push_back(it_loop->split_at_index(last_pos.nearest_point_index(*it_loop)));
        last_pos = paths.back().last_point();
    }

    // clip the paths to prevent the extruder from getting exactly on the first point of the loop
    // Keep valid paths only.
    size_t j = 0;
    for (size_t i = 0; i < paths.size(); ++ i) {
        paths[i].clip_end(this->loop_clipping);
        if (paths[i].is_valid()) {
            if (j < i)
                std::swap(paths[j], paths[i]);
            ++ j;
        }
    }
    if (j < paths.size())
        paths.erase(paths.begin() + j, paths.end());

    // TODO: return ExtrusionLoop objects to get better chained paths
    return paths;
}

} // namespace Slic3r
