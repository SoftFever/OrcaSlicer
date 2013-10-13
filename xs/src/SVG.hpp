#ifndef slic3r_SVG_hpp_
#define slic3r_SVG_hpp_

#include <myinit.h>
#include "Line.hpp"

namespace Slic3r {

class SVG
{
    private:
    FILE* f;
    public:
    SVG(const char* filename);
    void AddLine(const Line &line);
    void Close();
};

}

#endif
