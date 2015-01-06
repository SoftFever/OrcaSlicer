#ifndef slic3r_SVG_hpp_
#define slic3r_SVG_hpp_

#include <myinit.h>
#include "ExPolygon.hpp"
#include "Line.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

class SVG
{
    public:
    bool arrows;
    std::string fill, stroke;
    
    SVG(const char* filename);
    void AddLine(const Line &line);
    void AddLine(const IntersectionLine &line);
    void draw(const ExPolygon &expolygon);
    void draw(const Polygon &polygon);
    void draw(const Polyline &polyline);
    void Close();
    
    private:
    std::string filename;
    FILE* f;
    
    void path(const std::string &d, bool fill);
    std::string get_path_d(const MultiPoint &polygon) const;
};

}

#endif
