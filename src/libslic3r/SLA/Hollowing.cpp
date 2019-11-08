#include "Hollowing.hpp"
#include <openvdb/tools/Filter.h>
#include <openvdb/tools/LevelSetRebuild.h>
#include <openvdb/tools/LevelSetFilter.h>
#include <functional>

namespace Slic3r {
namespace sla {

namespace {

void filter_grid_sla(openvdb::FloatGrid &grid, double scale, double /*thickness*/, double flatness)
{
    static const double ROUNDNESS_COEFF = 1.;
    
    // Filtering:
    if (flatness > 0.) {
        double rounding = ROUNDNESS_COEFF * flatness;
        int width = int(rounding * scale);
        int count = 1;
        openvdb::tools::Filter<openvdb::FloatGrid>{grid}.gaussian(width, count);
    }
}
//    openvdb::tools::levelSetRebuild(grid, -float(thickness * 2));
//    filter_grid_sla(grid, scale, thickness, flatness);

//    openvdb::tools::levelSetRebuild(grid, float(thickness));


void redist_grid_sla(openvdb::FloatGrid &grid, double scale, double thickness, double flatness)
{
//    openvdb::tools::levelSetRebuild(grid, -float(scale * thickness));
    
    
    openvdb::tools::LevelSetFilter<openvdb::FloatGrid> filt{grid};
    
//    filt.gaussian(int(flatness * scale));
    
//    openvdb::tools::levelSetRebuild(grid, float(scale * thickness));
    //grid = openvdb::tools::topologyToLevelSet(grid);
}

}

TriangleMesh generate_interior(const TriangleMesh &mesh,
                               double              min_thickness,
                               double              quality,
                               double              flatness)
{
    namespace plc = std::placeholders;
    auto filt = std::bind(filter_grid_sla, plc::_1, plc::_2, plc::_3, flatness);
    return hollowed_interior(mesh, min_thickness, quality, filt);
}

}} // namespace Slic3r::sla
