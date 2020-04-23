
#include "SlicesToTriangleMesh.hpp"

#include "libslic3r/MTUtils.hpp"
#include "libslic3r/SLA/Contour3D.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Tesselate.hpp"

#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

namespace Slic3r {

inline sla::Contour3D wall_strip(const Polygon &poly,
                                 double         lower_z_mm,
                                 double         upper_z_mm)
{
    sla::Contour3D ret;
    
    size_t startidx = ret.points.size();
    size_t offs     = poly.points.size();
    
    ret.points.reserve(ret.points.size() + 2 *offs);
    
    for (const Point &p : poly.points)
        ret.points.emplace_back(to_3d(unscaled(p), lower_z_mm));
    
    for (const Point &p : poly.points)
        ret.points.emplace_back(to_3d(unscaled(p), upper_z_mm));
    
    for (size_t i = startidx + 1; i < startidx + offs; ++i) {
        ret.faces3.emplace_back(i - 1, i, i + offs - 1);
        ret.faces3.emplace_back(i, i + offs, i + offs - 1);
    }
    
    ret.faces3.emplace_back(startidx + offs - 1, startidx, startidx + 2 * offs - 1);
    ret.faces3.emplace_back(startidx, startidx + offs, startidx + 2 * offs - 1);
    
    return ret;
}

// Same as walls() but with identical higher and lower polygons.
sla::Contour3D inline straight_walls(const Polygon &plate,
                                     double         lo_z,
                                     double         hi_z)
{
    return wall_strip(plate, lo_z, hi_z);
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

sla::Contour3D slices_to_triangle_mesh(const std::vector<ExPolygons> &slices,
                                       double zmin,
                                       const std::vector<float> &    grid)
{
    assert(slices.size() == grid.size());

    using Layers = std::vector<sla::Contour3D>;
    std::vector<sla::Contour3D> layers(slices.size());
    size_t len = slices.size() - 1;

    tbb::parallel_for(size_t(0), len, [&slices, &layers, &grid](size_t i) {
        const ExPolygons &upper = slices[i + 1];
        const ExPolygons &lower = slices[i];

        ExPolygons dff1 = diff_ex(lower, upper);
        ExPolygons dff2 = diff_ex(upper, lower);
        layers[i].merge(triangulate_expolygons_3d(dff1, grid[i], NORMALS_UP));
        layers[i].merge(triangulate_expolygons_3d(dff2, grid[i], NORMALS_DOWN));
        layers[i].merge(straight_walls(upper, grid[i], grid[i + 1]));
        
    });
    
    sla::Contour3D ret = tbb::parallel_reduce(
        tbb::blocked_range(layers.begin(), layers.end()),
        sla::Contour3D{},
        [](const tbb::blocked_range<Layers::iterator>& r, sla::Contour3D init) {
            for(auto it = r.begin(); it != r.end(); ++it ) init.merge(*it);
            return init;
        },
        []( const sla::Contour3D &a, const sla::Contour3D &b ) {
            sla::Contour3D res{a}; res.merge(b); return res;
        });
    
    ret.merge(triangulate_expolygons_3d(slices.front(), zmin, NORMALS_DOWN));
    ret.merge(straight_walls(slices.front(), zmin, grid.front()));
    ret.merge(triangulate_expolygons_3d(slices.back(), grid.back(), NORMALS_UP));
        
    return ret;
}

void slices_to_triangle_mesh(TriangleMesh &                 mesh,
                             const std::vector<ExPolygons> &slices,
                             double                         zmin,
                             double                         lh,
                             double                         ilh)
{
    std::vector<sla::Contour3D> wall_meshes(slices.size());
    std::vector<float> grid(slices.size(), zmin + ilh);
    
    for (size_t i = 1; i < grid.size(); ++i) grid[i] = grid[i - 1] + lh;
    
    sla::Contour3D cntr = slices_to_triangle_mesh(slices, zmin, grid);
    mesh.merge(sla::to_triangle_mesh(cntr));
    mesh.repaired = true;
    mesh.require_shared_vertices();
}

} // namespace Slic3r
