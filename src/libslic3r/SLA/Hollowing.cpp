#include <functional>

//#include <openvdb/tools/Filter.h>
#include <boost/log/trivial.hpp>

#include "Hollowing.hpp"
#include <libslic3r/MTUtils.hpp>

namespace Slic3r {
namespace sla {

//namespace {

//void filter_grid_sla(openvdb::FloatGrid::Ptr &grid, double scale, double /*thickness*/, double flatness, double closing_dist)
//{
//    static const double ROUNDNESS_COEFF = 1.;
    
//    // Filtering:
//    if (flatness > 0.) {
//        double rounding = ROUNDNESS_COEFF * flatness;
//        int width = int(rounding * scale);
//        int count = 1;
//        openvdb::tools::Filter<openvdb::FloatGrid>{*grid}.gaussian(width, count);
//    }
//}

//}

template<class S, class = FloatingOnly<S>>
inline void _scale(S s, TriangleMesh &m) { m.scale(float(s)); }

template<class S, class = FloatingOnly<S>>
inline void _scale(S s, sla::Contour3D &m)
{
    for (auto &p : m.points) p *= s;
}

template<class Mesh>
remove_cvref_t<Mesh> _grid_to_mesh(const openvdb::FloatGrid &grid,
                                   double                    isosurf,
                                   double                    adapt);

template<>
TriangleMesh _grid_to_mesh<TriangleMesh>(const openvdb::FloatGrid &grid,
                                   double                    isosurf,
                                   double                    adapt)
{
    return grid_to_mesh(grid, isosurf, adapt);
}

template<>
sla::Contour3D _grid_to_mesh<sla::Contour3D>(const openvdb::FloatGrid &grid,
                                         double                    isosurf,
                                         double                    adapt)
{
    return grid_to_contour3d(grid, isosurf, adapt);
}

template<class Mesh>
remove_cvref_t<Mesh> _generate_interior(Mesh &&mesh,
                                        double min_thickness,
                                        double voxel_scale,
                                        double closing_dist)
{
//    namespace plc = std::placeholders;
//    auto filt = std::bind(redist_grid_sla, plc::_1, plc::_2, plc::_3, flatness, closing_dist);
//    return hollowed_interior(mesh, min_thickness, quality, filt);

    using MMesh = remove_cvref_t<Mesh>;
    MMesh imesh{std::forward<Mesh>(mesh)};
    
    _scale(voxel_scale, imesh);
    
    double offset = voxel_scale * min_thickness;
    double D = voxel_scale * closing_dist;
    float  out_range = 0.1f * float(offset);
    float  in_range = 1.1f * float(offset + D);
    auto gridptr = mesh_to_grid(imesh, {}, out_range, in_range);
    
    assert(gridptr);
    
    if (!gridptr) {
        BOOST_LOG_TRIVIAL(error) << "Returned OpenVDB grid is NULL";
        return MMesh{};
    }
    
    if (closing_dist > .0) {
        gridptr = redistance_grid(*gridptr, -(offset + D), double(in_range));
    } else {
        D = -offset;
    }
    
//    openvdb::tools::Filter<openvdb::FloatGrid> filt{*gridptr};
//    filt.offset(float(offset + D));
    
    double iso_surface = D;
    double adaptivity = 0.;
    auto omesh = _grid_to_mesh<MMesh>(*gridptr, iso_surface, adaptivity);
    
    _scale(1. / voxel_scale, omesh);
    
    return omesh;
}

TriangleMesh generate_interior(const TriangleMesh &mesh,
                               double              min_thickness,
                               double              quality,
                               double              closing_dist)
{
    static const double MAX_OVERSAMPL = 7.;
        
    // I can't figure out how to increase the grid resolution through openvdb API
    // so the model will be scaled up before conversion and the result scaled
    // down. Voxels have a unit size. If I set voxelSize smaller, it scales
    // the whole geometry down, and doesn't increase the number of voxels.
    //
    // max 8x upscale, min is native voxel size
    auto voxel_scale = (1.0 + MAX_OVERSAMPL * quality);
    return _generate_interior(mesh, min_thickness, voxel_scale, closing_dist);
}

}} // namespace Slic3r::sla
