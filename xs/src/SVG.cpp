#include "SVG.hpp"

namespace Slic3r {

SVG::SVG(const char* filename)
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
	this->arrows = true;
}

float
SVG::coordinate(long c)
{
    return (float)unscale(c)*10;
}

void
SVG::AddLine(const Line &line)
{
    fprintf(this->f,
        "   <line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\" style=\"stroke: black; stroke-width: 2\"",
        this->coordinate(line.a.x), this->coordinate(line.a.y), this->coordinate(line.b.x), this->coordinate(line.b.y)
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
SVG::Close()
{
    fprintf(this->f, "</svg>\n");
    fclose(this->f);
    printf("SVG file written.\n");
}

}
