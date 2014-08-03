#ifndef slic3r_SVG_hpp_
#define slic3r_SVG_hpp_

#include <myinit.h>
#include "Line.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

class SVG
{
    private:
    FILE* f;
    float coordinate(long c);
    public:
    bool arrows;
    SVG(const char* filename);
    void AddLine(const Line &line);
    void AddLine(const IntersectionLine &line);
    void Close();
};

}

#endif
