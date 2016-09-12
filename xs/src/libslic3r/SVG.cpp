#include "SVG.hpp"
#include <iostream>

#define COORD(x) ((float)unscale(x)*10)

namespace Slic3r {

SVG::SVG(const char* filename)
    : arrows(false), fill("grey"), stroke("black"), filename(filename)
{
    this->f = fopen(filename, "w");
    fprintf(this->f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n"
        "<svg height=\"2000\" width=\"2000\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n"
	    "   <marker id=\"endArrow\" markerHeight=\"8\" markerUnits=\"strokeWidth\" markerWidth=\"10\" orient=\"auto\" refX=\"1\" refY=\"5\" viewBox=\"0 0 10 10\">\n"
		"      <polyline fill=\"darkblue\" points=\"0,0 10,5 0,10 1,5\" />\n"
	    "   </marker>\n"
	    );
}

SVG::SVG(const char* filename, const BoundingBox &bbox)
    : arrows(false), fill("grey"), stroke("black"), filename(filename), origin(bbox.min)
{
    this->f = fopen(filename, "w");
    float w = COORD(bbox.max.x - bbox.min.x);
    float h = COORD(bbox.max.y - bbox.min.y);
    fprintf(this->f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n"
        "<svg height=\"%f\" width=\"%f\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n"
        "   <marker id=\"endArrow\" markerHeight=\"8\" markerUnits=\"strokeWidth\" markerWidth=\"10\" orient=\"auto\" refX=\"1\" refY=\"5\" viewBox=\"0 0 10 10\">\n"
        "      <polyline fill=\"darkblue\" points=\"0,0 10,5 0,10 1,5\" />\n"
        "   </marker>\n",
        h, w);
}

void
SVG::draw(const Line &line, std::string stroke, coord_t stroke_width)
{
    fprintf(this->f,
        "   <line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\" style=\"stroke: %s; stroke-width: %f\"",
        COORD(line.a.x - origin.x), COORD(line.a.y - origin.y), COORD(line.b.x - origin.x), COORD(line.b.y - origin.y), stroke.c_str(), (stroke_width == 0) ? 1.f : COORD(stroke_width));
    if (this->arrows)
        fprintf(this->f, " marker-end=\"url(#endArrow)\"");
    fprintf(this->f, "/>\n");
}

void SVG::draw(const ThickLine &line, const std::string &fill, const std::string &stroke, coord_t stroke_width)
{
    Pointf dir(line.b.x-line.a.x, line.b.y-line.a.y);
    Pointf perp(-dir.y, dir.x);
    coordf_t len = sqrt(perp.x*perp.x + perp.y*perp.y);
    coordf_t da  = coordf_t(0.5)*line.a_width/len;
    coordf_t db  = coordf_t(0.5)*line.b_width/len;
    fprintf(this->f,
        "   <polygon points=\"%f,%f %f,%f %f,%f %f,%f\" style=\"fill:%s; stroke: %s; stroke-width: %f\"/>\n",
        COORD(line.a.x-da*perp.x-origin.x),
        COORD(line.a.y-da*perp.y-origin.y),
        COORD(line.b.x-db*perp.x-origin.x),
        COORD(line.b.y-db*perp.y-origin.y),
        COORD(line.b.x+db*perp.x-origin.x),
        COORD(line.b.y+db*perp.y-origin.y),
        COORD(line.a.x+da*perp.x-origin.x),
        COORD(line.a.y+da*perp.y-origin.y),
        fill.c_str(), stroke.c_str(),
        (stroke_width == 0) ? 1.f : COORD(stroke_width));
}

void
SVG::draw(const Lines &lines, std::string stroke)
{
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it)
        this->draw(*it, stroke);
}

void
SVG::draw(const IntersectionLines &lines, std::string stroke)
{
    for (IntersectionLines::const_iterator it = lines.begin(); it != lines.end(); ++it)
        this->draw((Line)*it, stroke);
}

void
SVG::draw(const ExPolygon &expolygon, std::string fill)
{
    this->fill = fill;
    
    std::string d;
    Polygons pp = expolygon;
    for (Polygons::const_iterator p = pp.begin(); p != pp.end(); ++p) {
        d += this->get_path_d(*p, true) + " ";
    }
    this->path(d, true);
}

void
SVG::draw(const ExPolygons &expolygons, std::string fill)
{
    for (ExPolygons::const_iterator it = expolygons.begin(); it != expolygons.end(); ++it)
        this->draw(*it, fill);
}

void
SVG::draw(const Polygon &polygon, std::string fill)
{
    this->fill = fill;
    this->path(this->get_path_d(polygon, true), !fill.empty());
}

void
SVG::draw(const Polygons &polygons, std::string fill)
{
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++it)
        this->draw(*it, fill);
}

void
SVG::draw(const Polyline &polyline, std::string stroke, coord_t stroke_width)
{
    this->stroke = stroke;
    this->path(this->get_path_d(polyline, false), false, stroke_width);
}

void
SVG::draw(const Polylines &polylines, std::string stroke, coord_t stroke_width)
{
    for (Polylines::const_iterator it = polylines.begin(); it != polylines.end(); ++it)
        this->draw(*it, fill, stroke_width);
}

void SVG::draw(const ThickLines &thicklines, const std::string &fill, const std::string &stroke, coord_t stroke_width)
{
    for (ThickLines::const_iterator it = thicklines.begin(); it != thicklines.end(); ++it)
        this->draw(*it, fill, stroke, stroke_width);
}

void
SVG::draw(const ThickPolylines &polylines, const std::string &stroke, coord_t stroke_width)
{
    for (ThickPolylines::const_iterator it = polylines.begin(); it != polylines.end(); ++it)
        this->draw((Polyline)*it, stroke, stroke_width);
}

void 
SVG::draw(const ThickPolylines &thickpolylines, const std::string &fill, const std::string &stroke, coord_t stroke_width)
{
    for (ThickPolylines::const_iterator it = thickpolylines.begin(); it != thickpolylines.end(); ++ it)
        draw(it->thicklines(), fill, stroke, stroke_width);
}

void
SVG::draw(const Point &point, std::string fill, coord_t iradius)
{
    float radius = (iradius == 0) ? 3.f : COORD(iradius);
    std::ostringstream svg;
    svg << "   <circle cx=\"" << COORD(point.x - origin.x) << "\" cy=\"" << COORD(point.y - origin.y)
        << "\" r=\"" << radius << "\" "
        << "style=\"stroke: none; fill: " << fill << "\" />";
    
    fprintf(this->f, "%s\n", svg.str().c_str());
}

void
SVG::draw(const Points &points, std::string fill, coord_t radius)
{
    for (Points::const_iterator it = points.begin(); it != points.end(); ++it)
        this->draw(*it, fill, radius);
}

void
SVG::path(const std::string &d, bool fill, coord_t stroke_width)
{
    float lineWidth = 0.f;
    if (! fill)
        lineWidth = (stroke_width == 0) ? 2.f : COORD(stroke_width);

    fprintf(
        this->f,
        "   <path d=\"%s\" style=\"fill: %s; stroke: %s; stroke-width: %f; fill-type: evenodd\" %s />\n",
        d.c_str(),
        fill ? this->fill.c_str() : "none",
        this->stroke.c_str(),
        lineWidth, 
        (this->arrows && !fill) ? " marker-end=\"url(#endArrow)\"" : ""
    );
}

std::string
SVG::get_path_d(const MultiPoint &mp, bool closed) const
{
    std::ostringstream d;
    d << "M ";
    for (Points::const_iterator p = mp.points.begin(); p != mp.points.end(); ++p) {
        d << COORD(p->x - origin.x) << " ";
        d << COORD(p->y - origin.y) << " ";
    }
    if (closed) d << "z";
    return d.str();
}

void
SVG::Close()
{
    fprintf(this->f, "</svg>\n");
    fclose(this->f);
    this->f = NULL;
    printf("SVG written to %s\n", this->filename.c_str());
}

}
