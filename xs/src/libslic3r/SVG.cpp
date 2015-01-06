#include "SVG.hpp"
#include <iostream>

#define COORD(x) ((float)unscale(x)*10)

namespace Slic3r {

SVG::SVG(const char* filename)
    : arrows(true), filename(filename), fill("grey"), stroke("black")
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

void
SVG::AddLine(const Line &line)
{
    fprintf(this->f,
        "   <line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\" style=\"stroke: black; stroke-width: 2\"",
        COORD(line.a.x), COORD(line.a.y), COORD(line.b.x), COORD(line.b.y)
        );
    if (this->arrows)
        fprintf(this->f, " marker-end=\"url(#endArrow)\"");
    fprintf(this->f, "/>\n");
}

void
SVG::AddLine(const IntersectionLine &line)
{
    this->AddLine(Line(line.a, line.b));
}

void
SVG::draw(const ExPolygon &expolygon)
{
    std::string d;
    Polygons pp = expolygon;
    for (Polygons::const_iterator p = pp.begin(); p != pp.end(); ++p) {
        d += this->get_path_d(*p) + " ";
    }
    this->path(d, true);
}

void
SVG::draw(const Polygon &polygon)
{
    this->path(this->get_path_d(polygon), true);
}

void
SVG::draw(const Polyline &polyline)
{
    this->path(this->get_path_d(polyline), false);
}

void
SVG::path(const std::string &d, bool fill)
{
    fprintf(
        this->f,
        "   <path d=\"%s\" style=\"fill: %s; stroke: %s; stroke-width: %s; fill-type: evenodd\" />\n",
        d.c_str(),
        fill ? this->fill.c_str() : "none",
        this->stroke.c_str(),
        fill ? "0" : "2"
    );
}

std::string
SVG::get_path_d(const MultiPoint &mp) const
{
    std::ostringstream d;
    d << "M";
    for (Points::const_iterator p = mp.points.begin(); p != mp.points.end(); ++p) {
        d << " " << COORD(p->x);
        d << " " << COORD(p->y);
    }
    d << " z";
    return d.str();
}

void
SVG::Close()
{
    fprintf(this->f, "</svg>\n");
    fclose(this->f);
    printf("SVG written to %s\n", this->filename.c_str());
}

}
