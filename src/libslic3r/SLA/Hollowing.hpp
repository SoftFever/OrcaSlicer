#ifndef HOLLOWINGFILTER_HPP
#define HOLLOWINGFILTER_HPP

#include <libslic3r/OpenVDBUtils.hpp>

namespace Slic3r {
namespace sla {

TriangleMesh generate_interior(const TriangleMesh &mesh,
                               double              min_thickness,
                               double              quality  = 0.5,
                               double              flatness = 0.5);

}
}

#endif // HOLLOWINGFILTER_H
