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
    void draw(const Line &line, std::string stroke = "black");
    void draw(const Lines &lines, std::string stroke = "black");
    void draw(const IntersectionLines &lines, std::string stroke = "black");
    void draw(const ExPolygon &expolygon, std::string fill = "grey");
    void draw(const ExPolygons &expolygons, std::string fill = "grey");
    void draw(const Polygon &polygon, std::string fill = "grey");
    void draw(const Polygons &polygons, std::string fill = "grey");
    void draw(const Polyline &polyline, std::string stroke = "black");
    void draw(const Polylines &polylines, std::string stroke = "black");
    void draw(const Point &point, std::string fill = "black", unsigned int radius = 3);
    void draw(const Points &points, std::string fill = "black", unsigned int radius = 3);
    void Close();
    
    private:
    std::string filename;
    FILE* f;
    
    void path(const std::string &d, bool fill);
    std::string get_path_d(const MultiPoint &mp, bool closed = false) const;
};

}

#endif
