
#include "SlicesToTriangleMesh.hpp"

#include "libslic3r/TriangulateWall.hpp"
#include "libslic3r/SLA/Contour3D.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Tesselate.hpp"

namespace Slic3r {

inline sla::Contour3D walls(const Polygon &lower,
                            const Polygon &upper,
                            double         lower_z_mm,
                            double         upper_z_mm)
{
    Wall w = triangulate_wall(lower, upper, lower_z_mm, upper_z_mm);
    
    sla::Contour3D ret;
    ret.points = std::move(w.first);
    ret.faces3 = std::move(w.second);
    
    return ret;
}

// Same as walls() but with identical higher and lower polygons.
sla::Contour3D inline straight_walls(const Polygon &plate,
                                     double         lo_z,
                                     double         hi_z)
{
    return walls(plate, plate, lo_z, hi_z);
}

sla::Contour3D inline straight_walls(const ExPolygon &plate,
                                     double           lo_z,
                                     double           hi_z)
{
    sla::Contour3D ret;
    ret.merge(straight_walls(plate.contour, lo_z, hi_z));
    for (auto &h : plate.holes) ret.merge(straight_walls(h, lo_z, hi_z));
    return ret;
}

sla::Contour3D inline straight_walls(const ExPolygons &slice,
                                     double            lo_z,
                                     double            hi_z)
{
    sla::Contour3D ret;
    for (const ExPolygon &poly : slice)
        ret.merge(straight_walls(poly, lo_z, hi_z));
    
    return ret;
}

void slices_to_triangle_mesh(TriangleMesh &                 mesh,
                             const std::vector<ExPolygons> &slices,
                             double                         zmin,
                             double                         lh,
                             double                         ilh)
{
    sla::Contour3D cntr3d;
    double h = zmin;
    
    auto it = slices.begin(), xt = std::next(it);
    cntr3d.merge(triangulate_expolygons_3d(*it, h, NORMALS_DOWN));
    cntr3d.merge(straight_walls(*it, h, h + ilh));
    h += ilh;
    while (xt != slices.end()) {
        ExPolygons dff1 = diff_ex(*it, *xt);
        ExPolygons dff2 = diff_ex(*xt, *it);
        cntr3d.merge(triangulate_expolygons_3d(dff1, h, NORMALS_UP));
        cntr3d.merge(triangulate_expolygons_3d(dff2, h, NORMALS_UP));
        cntr3d.merge(straight_walls(*xt, h, h + lh));
        h += lh;
        ++it; ++xt;
    }
    
    cntr3d.merge(triangulate_expolygons_3d(*it, h, NORMALS_UP));
    
    mesh.merge(sla::to_triangle_mesh(cntr3d));
    mesh.require_shared_vertices();
}

} // namespace Slic3r
