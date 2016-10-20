#include "SVG.hpp"
#include <iostream>

#define COORD(x) ((float)unscale((x))*10)

namespace Slic3r {

bool SVG::open(const char* afilename)
{
    this->filename = afilename;
    this->f = fopen(afilename, "w");
    if (this->f == NULL)
        return false;
    fprintf(this->f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n"
        "<svg height=\"2000\" width=\"2000\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n"
        "   <marker id=\"endArrow\" markerHeight=\"8\" markerUnits=\"strokeWidth\" markerWidth=\"10\" orient=\"auto\" refX=\"1\" refY=\"5\" viewBox=\"0 0 10 10\">\n"
        "      <polyline fill=\"darkblue\" points=\"0,0 10,5 0,10 1,5\" />\n"
        "   </marker>\n"
        );
    return true;
}

bool SVG::open(const char* afilename, const BoundingBox &bbox, const coord_t bbox_offset, bool aflipY)
{
    this->filename = afilename;
    this->origin   = bbox.min - Point(bbox_offset, bbox_offset);
    this->flipY    = aflipY;
    this->f        = ::fopen(afilename, "w");
    if (f == NULL)
        return false;
    float w = COORD(bbox.max.x - bbox.min.x + 2 * bbox_offset);
    float h = COORD(bbox.max.y - bbox.min.y + 2 * bbox_offset);
    fprintf(this->f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n"
        "<svg height=\"%f\" width=\"%f\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n"
        "   <marker id=\"endArrow\" markerHeight=\"8\" markerUnits=\"strokeWidth\" markerWidth=\"10\" orient=\"auto\" refX=\"1\" refY=\"5\" viewBox=\"0 0 10 10\">\n"
        "      <polyline fill=\"darkblue\" points=\"0,0 10,5 0,10 1,5\" />\n"
        "   </marker>\n",
        h, w);
    return true;
}

void
SVG::draw(const Line &line, std::string stroke, coordf_t stroke_width)
{
    fprintf(this->f,
        "   <line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\" style=\"stroke: %s; stroke-width: %f\"",
        COORD(line.a.x - origin.x), COORD(line.a.y - origin.y), COORD(line.b.x - origin.x), COORD(line.b.y - origin.y), stroke.c_str(), (stroke_width == 0) ? 1.f : COORD(stroke_width));
    if (this->arrows)
        fprintf(this->f, " marker-end=\"url(#endArrow)\"");
    fprintf(this->f, "/>\n");
}

void SVG::draw(const ThickLine &line, const std::string &fill, const std::string &stroke, coordf_t stroke_width)
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
SVG::draw(const Lines &lines, std::string stroke, coordf_t stroke_width)
{
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it)
        this->draw(*it, stroke, stroke_width);
}

void
SVG::draw(const IntersectionLines &lines, std::string stroke)
{
    for (IntersectionLines::const_iterator it = lines.begin(); it != lines.end(); ++it)
        this->draw((Line)*it, stroke);
}

void
SVG::draw(const ExPolygon &expolygon, std::string fill, const float fill_opacity)
{
    this->fill = fill;
    
    std::string d;
    Polygons pp = expolygon;
    for (Polygons::const_iterator p = pp.begin(); p != pp.end(); ++p) {
        d += this->get_path_d(*p, true) + " ";
    }
    this->path(d, true, 0, fill_opacity);
}

void 
SVG::draw_outline(const ExPolygon &expolygon, std::string stroke_outer, std::string stroke_holes, coordf_t stroke_width)
{
    draw_outline(expolygon.contour, stroke_outer, stroke_width);
    for (Polygons::const_iterator it = expolygon.holes.begin(); it != expolygon.holes.end(); ++ it) {
        draw_outline(*it, stroke_holes, stroke_width);
    }
}

void
SVG::draw(const ExPolygons &expolygons, std::string fill, const float fill_opacity)
{
    for (ExPolygons::const_iterator it = expolygons.begin(); it != expolygons.end(); ++it)
        this->draw(*it, fill, fill_opacity);
}

void 
SVG::draw_outline(const ExPolygons &expolygons, std::string stroke_outer, std::string stroke_holes, coordf_t stroke_width)
{
    for (ExPolygons::const_iterator it = expolygons.begin(); it != expolygons.end(); ++ it)
        draw_outline(*it, stroke_outer, stroke_holes, stroke_width);
}

void
SVG::draw(const Surface &surface, std::string fill, const float fill_opacity)
{
    draw(surface.expolygon, fill, fill_opacity);
}

void 
SVG::draw_outline(const Surface &surface, std::string stroke_outer, std::string stroke_holes, coordf_t stroke_width)
{
    draw_outline(surface.expolygon, stroke_outer, stroke_holes, stroke_width);
}

void
SVG::draw(const Surfaces &surfaces, std::string fill, const float fill_opacity)
{
    for (Surfaces::const_iterator it = surfaces.begin(); it != surfaces.end(); ++it)
        this->draw(*it, fill, fill_opacity);
}

void 
SVG::draw_outline(const Surfaces &surfaces, std::string stroke_outer, std::string stroke_holes, coordf_t stroke_width)
{
    for (Surfaces::const_iterator it = surfaces.begin(); it != surfaces.end(); ++ it)
        draw_outline(*it, stroke_outer, stroke_holes, stroke_width);
}

void
SVG::draw(const SurfacesPtr &surfaces, std::string fill, const float fill_opacity)
{
    for (SurfacesPtr::const_iterator it = surfaces.begin(); it != surfaces.end(); ++it)
        this->draw(*(*it), fill, fill_opacity);
}

void 
SVG::draw_outline(const SurfacesPtr &surfaces, std::string stroke_outer, std::string stroke_holes, coordf_t stroke_width)
{
    for (SurfacesPtr::const_iterator it = surfaces.begin(); it != surfaces.end(); ++ it)
        draw_outline(*(*it), stroke_outer, stroke_holes, stroke_width);
}

void
SVG::draw(const Polygon &polygon, std::string fill)
{
    this->fill = fill;
    this->path(this->get_path_d(polygon, true), !fill.empty(), 0, 1.f);
}

void
SVG::draw(const Polygons &polygons, std::string fill)
{
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++it)
        this->draw(*it, fill);
}

void
SVG::draw(const Polyline &polyline, std::string stroke, coordf_t stroke_width)
{
    this->stroke = stroke;
    this->path(this->get_path_d(polyline, false), false, stroke_width, 1.f);
}

void
SVG::draw(const Polylines &polylines, std::string stroke, coordf_t stroke_width)
{
    for (Polylines::const_iterator it = polylines.begin(); it != polylines.end(); ++it)
        this->draw(*it, stroke, stroke_width);
}

void SVG::draw(const ThickLines &thicklines, const std::string &fill, const std::string &stroke, coordf_t stroke_width)
{
    for (ThickLines::const_iterator it = thicklines.begin(); it != thicklines.end(); ++it)
        this->draw(*it, fill, stroke, stroke_width);
}

void
SVG::draw(const ThickPolylines &polylines, const std::string &stroke, coordf_t stroke_width)
{
    for (ThickPolylines::const_iterator it = polylines.begin(); it != polylines.end(); ++it)
        this->draw((Polyline)*it, stroke, stroke_width);
}

void 
SVG::draw(const ThickPolylines &thickpolylines, const std::string &fill, const std::string &stroke, coordf_t stroke_width)
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
SVG::draw(const ClipperLib::Path &polygon, double scale, std::string stroke, coordf_t stroke_width)
{
    this->stroke = stroke;
    this->path(this->get_path_d(polygon, scale, true), false, stroke_width, 1.f);
}

void 
SVG::draw(const ClipperLib::Paths &polygons, double scale, std::string stroke, coordf_t stroke_width)
{
    for (ClipperLib::Paths::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        draw(*it, scale, stroke, stroke_width);
}

void 
SVG::draw_outline(const Polygon &polygon, std::string stroke, coordf_t stroke_width)
{
    this->stroke = stroke;
    this->path(this->get_path_d(polygon, true), false, stroke_width, 1.f);
}

void 
SVG::draw_outline(const Polygons &polygons, std::string stroke, coordf_t stroke_width)
{
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        draw_outline(*it, stroke, stroke_width);
}

void
SVG::path(const std::string &d, bool fill, coordf_t stroke_width, const float fill_opacity)
{
    float lineWidth = 0.f;
    if (! fill)
        lineWidth = (stroke_width == 0) ? 2.f : COORD(stroke_width);

    fprintf(
        this->f,
        "   <path d=\"%s\" style=\"fill: %s; stroke: %s; stroke-width: %f; fill-type: evenodd\" %s fill-opacity=\"%f\" />\n",
        d.c_str(),
        fill ? this->fill.c_str() : "none",
        this->stroke.c_str(),
        lineWidth, 
        (this->arrows && !fill) ? " marker-end=\"url(#endArrow)\"" : "",
        fill_opacity
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

std::string
SVG::get_path_d(const ClipperLib::Path &path, double scale, bool closed) const
{
    std::ostringstream d;
    d << "M ";
    for (ClipperLib::Path::const_iterator p = path.begin(); p != path.end(); ++p) {
        d << COORD(scale * p->X - origin.x) << " ";
        d << COORD(scale * p->Y - origin.y) << " ";
    }
    if (closed) d << "z";
    return d.str();
}

void SVG::draw_text(const Point &pt, const char *text, const char *color)
{
    fprintf(this->f,
        "<text x=\"%f\" y=\"%f\" font-family=\"sans-serif\" font-size=\"20px\" fill=\"%s\">%s</text>",
        COORD(pt.x-origin.x),
        COORD(pt.y-origin.y),
        color, text);
}

void SVG::draw_legend(const Point &pt, const char *text, const char *color)
{
    fprintf(this->f,
        "<circle cx=\"%f\" cy=\"%f\" r=\"10\" fill=\"%s\"/>",
        COORD(pt.x-origin.x),
        COORD(pt.y-origin.y),
        color);
    fprintf(this->f,
        "<text x=\"%f\" y=\"%f\" font-family=\"sans-serif\" font-size=\"10px\" fill=\"%s\">%s</text>",
        COORD(pt.x-origin.x) + 20.f,
        COORD(pt.y-origin.y),
        "black", text);
}

void
SVG::Close()
{
    fprintf(this->f, "</svg>\n");
    fclose(this->f);
    this->f = NULL;
//    printf("SVG written to %s\n", this->filename.c_str());
}

void SVG::export_expolygons(const char *path, const BoundingBox &bbox, const Slic3r::ExPolygons &expolygons, std::string stroke_outer, std::string stroke_holes, coordf_t stroke_width)
{
    SVG svg(path, bbox);
    svg.draw(expolygons);
    svg.draw_outline(expolygons, stroke_outer, stroke_holes, stroke_width);
    svg.Close();
}

}
