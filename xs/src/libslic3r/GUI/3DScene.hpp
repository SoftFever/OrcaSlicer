#ifndef slic3r_3DScene_hpp_
#define slic3r_3DScene_hpp_

#include <myinit.h>
#include "../Point.hpp"
#include "../Line.hpp"

namespace Slic3r {

class _3DScene
{
    public:
    static void _extrusionentity_to_verts_do(const Lines &lines, const std::vector<double> &widths,
        const std::vector<double> &heights, bool closed, double top_z, const Point &copy,
        Pointf3s* qverts, Pointf3s* qnorms, Pointf3s* tverts, Pointf3s* tnorms);
};

}

#endif
