#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include <vector>
#include "Polyline.hpp"

namespace Slic3r {

class Polygon : public Polyline {
    protected:
    char* perl_class() {
        return (char*)"Slic3r::Polygon";
    }
};

typedef std::vector<Polygon> Polygons;

}

#endif
