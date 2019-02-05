#ifndef slic3r_Tesselate_hpp_
#define slic3r_Tesselate_hpp_

#include <vector>

namespace Slic3r {

class ExPolygon;
typedef std::vector<ExPolygon> ExPolygons;

extern Pointf3s triangulate_expolygons_3df(const ExPolygon &poly, coordf_t z = 0, bool flip = false);
extern Pointf3s triangulate_expolygons_3df(const ExPolygons &polys, coordf_t z = 0, bool flip = false);

} // namespace Slic3r

#endif /* slic3r_Tesselate_hpp_ */
