#ifndef slic3r_SurfaceCollection_hpp_
#define slic3r_SurfaceCollection_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "Surface.hpp"

namespace Slic3r {

typedef std::vector<Surface> Surfaces;

class SurfaceCollection
{
    public:
    Surfaces surfaces;
    SV* arrayref();
};

}

#endif
