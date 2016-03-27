#ifndef slic3r_SVG_hpp_
#define slic3r_SVG_hpp_

#include "libslic3r.h"
#include "ExPolygon.hpp"
#include "Line.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

class SVG
{
    public:
    bool arrows;
    std::string fill, stroke;
    Point origin;

    SVG(const char* filename);
    SVG(const char* filename, const BoundingBox &bbox);
    void draw(const Line &line, std::string stroke = "black", coord_t stroke_width = 0);
    void draw(const ThickLine &line, const std::string &fill, const std::string &stroke, coord_t stroke_width = 0);
    void draw(const Lines &lines, std::string stroke = "black");
    void draw(const IntersectionLines &lines, std::string stroke = "black");
    void draw(const ExPolygon &expolygon, std::string fill = "grey");
    void draw(const ExPolygons &expolygons, std::string fill = "grey");
    void draw(const Polygon &polygon, std::string fill = "grey");
    void draw(const Polygons &polygons, std::string fill = "grey");
    void draw(const Polyline &polyline, std::string stroke = "black", coord_t stroke_width = 0);
    void draw(const Polylines &polylines, std::string stroke = "black", coord_t stroke_width = 0);
    void draw(const ThickLines &thicklines, const std::string &fill = "lime", const std::string &stroke = "black", coord_t stroke_width = 0);
    void draw(const ThickPolylines &polylines, const std::string &stroke = "black", coord_t stroke_width = 0);
    void draw(const ThickPolylines &thickpolylines, const std::string &fill, const std::string &stroke, coord_t stroke_width);
    void draw(const Point &point, std::string fill = "black", coord_t radius = 0);
    void draw(const Points &points, std::string fill = "black", coord_t radius = 0);
    void Close();
    
    private:
    std::string filename;
    FILE* f;
    
    void path(const std::string &d, bool fill, coord_t stroke_width = 0);
    std::string get_path_d(const MultiPoint &mp, bool closed = false) const;
};

}

#endif
