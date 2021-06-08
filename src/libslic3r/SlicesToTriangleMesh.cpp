
#include "SlicesToTriangleMesh.hpp"

//#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Execution/ExecutionTBB.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Tesselate.hpp"

#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

namespace Slic3r {

inline indexed_triangle_set wall_strip(const Polygon &poly,
                                       double         lower_z_mm,
                                       double         upper_z_mm)
{
    indexed_triangle_set ret;
    
    size_t startidx = ret.vertices.size();
    size_t offs     = poly.points.size();
    
    ret.vertices.reserve(ret.vertices.size() + 2 *offs);
    
    for (const Point &p : poly.points)
        ret.vertices.emplace_back(to_3d(unscaled<float>(p), float(lower_z_mm)));
    
    for (const Point &p : poly.points)
        ret.vertices.emplace_back(to_3d(unscaled<float>(p), float(upper_z_mm)));
    
    for (size_t i = startidx + 1; i < startidx + offs; ++i) {
        ret.indices.emplace_back(i - 1, i, i + offs - 1);
        ret.indices.emplace_back(i, i + offs, i + offs - 1);
    }
    
    ret.indices.emplace_back(startidx + offs - 1, startidx, startidx + 2 * offs - 1);
    ret.indices.emplace_back(startidx, startidx + offs, startidx + 2 * offs - 1);
    
    return ret;
}

// Same as walls() but with identical higher and lower polygons.
indexed_triangle_set inline straight_walls(const Polygon &plate,
                                     double         lo_z,
                                     double         hi_z)
{
    return wall_strip(plate, lo_z, hi_z);
}

indexed_triangle_set inline straight_walls(const ExPolygon &plate,
                                     double           lo_z,
                                     double           hi_z)
{
    indexed_triangle_set ret = straight_walls(plate.contour, lo_z, hi_z);
    for (auto &h : plate.holes)
        its_merge(ret, straight_walls(h, lo_z, hi_z));

    return ret;
}

indexed_triangle_set inline straight_walls(const ExPolygons &slice,
                                     double            lo_z,
                                     double            hi_z)
{
    indexed_triangle_set ret;
    for (const ExPolygon &poly : slice)
        its_merge(ret, straight_walls(poly, lo_z, hi_z));

    return ret;
}

indexed_triangle_set slices_to_mesh(
    const std::vector<ExPolygons> &slices,
    double                         zmin,
    const std::vector<float> &     grid)
{
    assert(slices.size() == grid.size());

    using Layers = std::vector<indexed_triangle_set>;
    Layers layers(slices.size());
    size_t len = slices.size() - 1;

    tbb::parallel_for(size_t(0), len, [&slices, &layers, &grid](size_t i) {
        const ExPolygons &upper = slices[i + 1];
        const ExPolygons &lower = slices[i];

        ExPolygons dff1 = diff_ex(lower, upper);
        ExPolygons dff2 = diff_ex(upper, lower);
        its_merge(layers[i], triangulate_expolygons_3d(dff1, grid[i], NORMALS_UP));
        its_merge(layers[i], triangulate_expolygons_3d(dff2, grid[i], NORMALS_DOWN));
        its_merge(layers[i], straight_walls(upper, grid[i], grid[i + 1]));
        
    });

    auto merge_fn = []( const indexed_triangle_set &a, const indexed_triangle_set &b ) {
        indexed_triangle_set res{a}; its_merge(res, b); return res;
    };

    auto ret = execution::reduce(ex_tbb, layers.begin(), layers.end(),
                                 indexed_triangle_set{}, merge_fn);

    //    sla::Contour3D ret = tbb::parallel_reduce(
    //        tbb::blocked_range(layers.begin(), layers.end()),
    //        sla::Contour3D{},
    //        [](const tbb::blocked_range<Layers::iterator>& r, sla::Contour3D
    //        init) {
    //            for(auto it = r.begin(); it != r.end(); ++it )
    //            init.merge(*it); return init;
    //        },
    //        []( const sla::Contour3D &a, const sla::Contour3D &b ) {
    //            sla::Contour3D res{a}; res.merge(b); return res;
    //        });
    
    its_merge(ret, triangulate_expolygons_3d(slices.front(), zmin, NORMALS_DOWN));
    its_merge(ret, straight_walls(slices.front(), zmin, grid.front()));
    its_merge(ret, triangulate_expolygons_3d(slices.back(), grid.back(), NORMALS_UP));
        
    return ret;
}

void slices_to_mesh(indexed_triangle_set &         mesh,
                             const std::vector<ExPolygons> &slices,
                             double                         zmin,
                             double                         lh,
                             double                         ilh)
{
    std::vector<indexed_triangle_set> wall_meshes(slices.size());
    std::vector<float> grid(slices.size(), zmin + ilh);
    
    for (size_t i = 1; i < grid.size(); ++i)
        grid[i] = grid[i - 1] + lh;
    
    indexed_triangle_set cntr = slices_to_mesh(slices, zmin, grid);
    its_merge(mesh, cntr);
}

} // namespace Slic3r
