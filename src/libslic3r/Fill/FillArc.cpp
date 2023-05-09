#include "FillArc.hpp"
#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Polygon.hpp"
#include "../Polyline.hpp"
#include "../Surface.hpp"

namespace Slic3r {

void FillArc::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction,
    const Polyline  pedestal,
    ExPolygon                        expolygon,
    Polylines                       &polylines_out) const
{
    // no rotation is supported for this infill pattern
    BoundingBox bbox = expolygon.contour.bounding_box();
    
    /*if (params.density > 0.9999f && !params.dont_adjust) {
        //it's == this->_adjust_solid_spacing(bounding_box.size()(0), _line_spacing_for_density(params.density)) because of the init_spacing()
        distance = scale_(this->get_spacing());
    }
    
    

    //if (params.density > 0.9999f && !params.dont_adjust) {
        //cf init_spacing
    //} else {
        // extend bounding box so that our pattern will be aligned with other layers
        // Transform the reference point to the rotated coordinate system.100
        bbox.merge(align_to_grid(
            bbox.min,
            Point(this->_line_spacing, this->_line_spacing),
            direction.second.rotated(-direction.first)));
    }*/
    coord_t min_spacing = scale_(this->get_spacing())*.75;
    coord_t distance = coord_t(min_spacing / params.density);
    //int bridge_sign = (int)std::floor((direction.first-.5*M_PI) / (M_PI * 2) * 3);
    /*if(bridge_sign <2 ) 
         bridge_sign +=  1;
     else 
         bridge_sign -= 1;*/
    double bridge_angl = (int(direction.first*100)%int(2*M_PI))/100;
    coord_t radius = std::max(bbox.max(0)-bbox.min(0),bbox.max(1)-bbox.min(1))*params.config->arc_radius;
    double center_x = bbox.min(0) + bbox.size()(0)/2;//*std::cos(direction.first);//* bridge_angl;// * angleSignTable[bridge_sign][0];//*std::abs((angleSignTable[bridge_sign][0]>0?1:0)-bridge_angl);*/
    double center_y = bbox.min(1) + bbox.size()(1)/2;//*std::sin(direction.first);//* bridge_angl;//+ radius;// * angleSignTable[bridge_sign][1];//*std::abs((angleSignTable[bridge_sign][0]>0?0:1)-bridge_angl);*/
    // do a raytrace to find the place where the angle from center hits the contour and translate the arc's center to that place
    Polyline intersect_line;
    Polylines intersect_lines;
    ClipperLib::Clipper clipper;
    Vec2d pt;
    double angle = direction.first-.5*M_PI;
    if(pedestal.empty()){
        intersect_line.points.emplace_back((center_x),(center_y));
        intersect_line.points.emplace_back(((double)(center_x + bbox.size().x()*double(params.config->arc_infill_raylen.value)*std::cos(1*angle)))
                                ,((double)(center_y + bbox.size().y()*double(params.config->arc_infill_raylen.value)*std::sin(1*angle))));
        intersect_line.points.emplace_back(((double)(center_x + bbox.size().x()*(double(params.config->arc_infill_raylen.value)+.1)*std::cos(1*angle)))
                                ,((double)(center_y + bbox.size().y()*(double(params.config->arc_infill_raylen.value)+.1)*std::sin(1*angle))));
        intersect_line.points.emplace_back((center_x),(center_y));
        intersect_lines = intersection_pl(intersect_line,expolygon);

        // clipper.AddPath(contour, ClipperLib::ptSubject, false);
        // clipper.AddPath(intersect_line, ClipperLib::ptClip, true);
        // clipper.Execute(ClipperLib::ctIntersection, polytree0);
        // intersect_lines = PolyTreeToPolylines(std::move(polytree0));
        if(!intersect_lines.empty()){
            center_x = (direction.first<M_PI||direction.first>2*M_PI)?(std::max(std::max(intersect_lines.front().back().x(),intersect_lines.front().front().x()),std::max(intersect_lines.back().back().x(),intersect_lines.back().front().x()))):
                        (std::min(std::min(intersect_lines.front().back().x(),intersect_lines.front().front().x()),std::min(intersect_lines.back().back().x(),intersect_lines.back().front().x())));
            center_y = (direction.first<1.5*M_PI)?(std::max(std::max(intersect_lines.front().back().y(),intersect_lines.front().front().y()),std::max(intersect_lines.back().back().y(),intersect_lines.back().front().y()))):
                        (std::min(std::min(intersect_lines.front().back().y(),intersect_lines.front().front().y()),std::min(intersect_lines.back().back().y(),intersect_lines.back().front().y())));
        }else{
        //clipper.Clear();
        intersect_line.clear();
        intersect_line.points.emplace_back((center_x),(center_y));
        intersect_line.points.emplace_back(((double)(bbox.size().x()*double(params.config->arc_infill_raylen.value)*std::cos(-1*angle)))
                                ,((double)(bbox.size().y()*double(params.config->arc_infill_raylen.value)*std::sin(-1*angle))));
        intersect_line.points.emplace_back(((double)(bbox.size().x()*(double(params.config->arc_infill_raylen.value)+.1)*std::cos(-1*angle)))
                                ,((double)(bbox.size().y()*(double(params.config->arc_infill_raylen.value)+.1)*std::sin(-1*angle))));
        intersect_line.points.emplace_back((center_x),(center_y));
        intersect_lines = intersection_pl(intersect_line,expolygon);
        // clipper.AddPath(contour, ClipperLib::ptSubject, false);
        // clipper.AddPath(intersect_line, ClipperLib::ptClip, true);
        // clipper.Execute(ClipperLib::ctIntersection, polytree0);
        // intersect_lines = PolyTreeToPolylines(std::move(polytree0));
        if(!intersect_lines.empty()){
         center_x = (direction.first<M_PI||direction.first>2*M_PI)?(std::max(std::max(intersect_lines.front().back().x(),intersect_lines.front().front().x()),std::max(intersect_lines.back().back().x(),intersect_lines.back().front().x()))):
                        (std::min(std::min(intersect_lines.front().back().x(),intersect_lines.front().front().x()),std::min(intersect_lines.back().back().x(),intersect_lines.back().front().x())));
            center_y = (direction.first<1.5*M_PI)?(std::max(std::max(intersect_lines.front().back().y(),intersect_lines.front().front().y()),std::max(intersect_lines.back().back().y(),intersect_lines.back().front().y()))):
                        (std::min(std::min(intersect_lines.front().back().y(),intersect_lines.front().front().y()),std::min(intersect_lines.back().back().y(),intersect_lines.back().front().y())));
        }else{
           center_x = bbox.center().x()+bbox.size().x()/2*std::cos((angle+(bridge_angl)*M_PI));
           center_y = bbox.center().y()+bbox.size().y()/2*std::sin((angle+(bridge_angl)*M_PI));
         }
        }
        ExPolygon expolygons;
        for (double angle=0.; angle<=2.05*M_PI; angle+=0.05*M_PI)
            expolygons.contour.points.push_back(Point((double)center_x + radius * cos(angle),
                                                (double)center_y + radius * sin(angle)));
        //expolygons.rotate(direction.first+0.5*M_PI,expolygons.contour.bounding_box().center());
	/*ClipperLib::Path contour;
        ClipperLib::PolyTree polytree0;
        ClipperLib::PolyTree polytree1;
        ClipperLib::PolyTree polytree2;
        */
        /*for (const Point &point : expolygon.contour.points) {
            pt = point.cast<double>();
            //add_offset_point(pt);
            pt += Vec2d(0.5 - (pt.x() < 0), 0.5 - (pt.y() < 0));
            contour.emplace_back(ClipperLib::cInt(pt.x()),
                                ClipperLib::cInt(pt.y()));
        }*/
        //clipper.Clear();
        Polygons  loops = to_polygons(expolygons);
        ExPolygons  last { std::move(expolygons) };
        /*ClipperLib::Paths holes;    
        ClipperLib::Path hole_temp;
        for (const Polygon &hole : expolygon.holes) {
            for (const Point &point : hole.points) {
                pt = point.cast<double>();
                // add_offset_point(pt);
                pt += Vec2d(0.5 - (pt.x() < 0), 0.5 - (pt.y() < 0));
                hole_temp.emplace_back(ClipperLib::cInt(pt.x()),
                                ClipperLib::cInt(pt.y()));
            }
            holes.emplace_back(std::move(hole_temp));
            hole_temp.clear();
        }*/
        //ClipperLib::Path     line0;
        //ClipperLib::Path     line;
        //ClipperLib::Paths     lines;    
        Polyline line;
	Polyline line0;
	Polylines lines;
	Polylines polylines;
	//clipper.AddPath(contour, ClipperLib::ptClip, true);
        // make the new outlines of the circles
        while (!last.empty()) {
            int i = 0;
            last = offset2_ex(last, -(distance + min_spacing/2), +min_spacing/2);//,ClipperLib::jtRound);  
            //if (last.at(0).is_valid()) {
                //line.reserve(last[0].contour.points.size() * 2);
            for (ExPolygons::const_iterator it = last.begin(); it != last.end();
                ++it) {
            line.clear();
            line0.clear();
            for (const Point &point : it->contour.points) {
                pt = point.cast<double>();
                // add_offset_line(pt);
                /*pt += Vec2d(0.5 - (pt.x() < 0),
                            0.5 - (pt.y() < 0));*/
                if(!(!expolygon.holes.empty() &&(expolygon.contains_h(direction.second)))
                    && i++<it->contour.points.size()*std::tan((direction.first-.25*M_PI)/6)) line0.points.push_back(Point(pt.x(),pt.y()));
                else
			line.points.push_back(Point(pt.x(),pt.y()));/*ClipperLib::cInt(pt.x()),
                                    ClipperLib::cInt(pt.y()));
            */}
            if(!line0.empty()) for (const Point &point : line0) {
                pt = point.cast<double>();
                // add_offset_line(pt);
                //pt += Vec2d(0.5 - (pt.x() < 0),
                //            0.5 - (pt.y() < 0));
                line.points.push_back(Point(pt.x(),pt.y()));
            }
            if(line.points.front()!=line.points.back()) line.points.push_back(line.points.front());
            lines.push_back(line);
            }
        }
        //if(((lines.size()>2 && (lines.at(std::floor(lines.size()*.6)).size())<(lines.at(std::floor(lines.size()*.3)).size())))) 
        //std::reverse(lines.begin(), lines.end());
	polylines = intersection_pl(lines, expolygon);
        //polylines = diff_pl(polylines,expolygon.holes);
	std::sort(polylines.begin(), polylines.end(),
              [center_x,center_y](const auto &i1, const auto &i2) {
		  return i1.distance_from_edges(Point(center_x,center_y)) < i2.distance_from_edges(Point(center_x,center_y));
              });
	    //Polylines chained;
        //Point ptc(center_x,center_y);
        //if (params.dont_connect() || params.density > 0.5 || polylines.size() <= 1)
         //       chained = chain_polylines(std::move(polylines),&ptc);
        //else
        //  connect_infill(std::move(polylines), expolygon, chained, min_spacing, params);
        append(polylines_out, std::move(polylines));//.size()==chained.size()?chained:polylines));
        // clip the line using the contours
        /*clipper.AddPaths(lines, ClipperLib::ptSubject, false);
        clipper.Execute(ClipperLib::ctIntersection, polytree1);
        ClipperLib::Paths paths;
        PolyTreeToPaths(std::move(polytree1),paths);
        clipper.Clear();
        clipper.AddPaths(paths, ClipperLib::ptSubject, false);
        clipper.AddPaths(holes, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctDifference, polytree2);
        //loops = to_polygons(PolyTreeToExPolygons(std::move(polytree)));
        Polylines fill_lines = PolyTreeToPolylines(std::move(polytree2));
        //std::reverse(fill_lines.begin(), fill_lines.end());
        polylines_out = std::move(fill_lines);
        if(!((polylines_out.size()>2 && (polylines_out.at(std::floor(polylines_out.size()*.6)).size())<(polylines_out.at(std::floor(polylines_out.size()*.3)).size())))) 
            std::reverse(polylines_out.begin(), polylines_out.end());
        if (!polylines_out.empty()) { // prevent calling leftmost_point() on empty
                                    // collections
            Polylines polylines_conn;
            bool first = true;
            bool invert = false;
            Point pt(center_x,center_y);
            for (Polyline &polyline : chain_polylines(std::move(polylines_out),&pt)) {
                if (! first) {
                    // Try to connect the previous end to the new start.
                    Points &pts_end = polylines_conn.back().points;
                    const Point &first_point = invert?polyline.points.back():polyline.points.front();
                    const Point &last_point = pts_end.back();
                    // Distance in X, Y.
                    const Vector distance = last_point - first_point;
                    // TODO: we should also check that both points are on a fill_boundary to avoid 
                    // connecting paths on the boundaries of internal regions
                    if (this->_can_connect(std::abs(distance(0)), std::abs(distance(1))) && 
                        expolygon.contains(Line(last_point, first_point))) {
                        // Append the polyline.
                        pts_end.insert(pts_end.end(), invert?polyline.points.end():polyline.points.begin(), invert?polyline.points.begin():polyline.points.end());
                        //invert = !invert;
                        continue;
                    }
                }
                if(invert) std::reverse(polyline.points.begin(),polyline.points.end());
                // The lines cannot be connected.
                polylines_conn.emplace_back(std::move(polyline));
                first = false;
                //invert = !invert;
            }
            polylines_out = /*chain_lines(to_lines(std::move(polylines_conn)),0.1);std::move(polylines_conn);
        }*/
    }else{
        center_x = pedestal.bounding_box().center().x();
        center_y = pedestal.bounding_box().center().y();
        // Now unwind the spiral.
        Pointfs out;
        Polylines polylines;
        //FIXME Vojtech: If used as a solid infill, there is a gap left at the center.
        // Radius to achieve.
        coord_t min_x = coord_t(ceil(coordf_t(bbox.min.x()) / distance));
        coord_t min_y = coord_t(ceil(coordf_t(bbox.min.y()) / distance));
        coord_t max_x = coord_t(ceil(coordf_t(bbox.max.x()) / distance));
        coord_t max_y = coord_t(ceil(coordf_t(bbox.max.y()) / distance));
        coordf_t rmax = std::sqrt(coordf_t(max_x)*coordf_t(max_x)+coordf_t(max_y)*coordf_t(max_y)) * std::sqrt(3.) + 1.5;
        // Now unwind the spiral.
        //coordf_t a = 1.;
        coordf_t b = 1./(2.*M_PI);
        coordf_t theta = 0.;
        coordf_t r = 1;
	out.emplace_back(0,0);
        //FIXME Vojtech: If used as a solid infill, there is a gap left at the center.
        while (r < rmax) {
            // Discretization angle to achieve a discretization error lower than resolution.
            theta += 2. * acos(1. - params.resolution / r);
            r = /*a + */b * theta;
            out.emplace_back(r * cos(theta), r * sin(theta));
        }
        if (out.size() >= 2) {
            // Convert points to a polyline, upscale.
            Polylines polylines(1, Polyline());
            Polyline &polyline = polylines.front();
            polyline.points.reserve(out.size());
            for (const Vec2d &pt : out)
                polyline.points.emplace_back(
                    coord_t(floor(center_x + pt.x() * distance + 0.5)),
                    coord_t(floor(center_y + pt.y() * distance + 0.5)));
            polylines = intersection_pl(polylines, expolygon);
	    std::sort(polylines.begin(), polylines.end(),
              [center_x,center_y](const auto &i1, const auto &i2) {
                  return i1.distance_to(Point(center_x,center_y)) < i2.distance_to(Point(center_x,center_y));/* .at(0).x() >
                             i2.at(0).x() &&
                         i1.at(0).y() > i2.at(0).y();*/
              });
            //polylines = diff_pl(polylines,expolygon.holes);
            //Polylines chained;
            //Point pt(center_x,center_y);
            /*if (params.dont_connect() || params.density > 0.5 || polylines.size() <= 1)
                chained = chain_polylines(std::move(polylines),&pt);
            else
               connect_infill(std::move(polylines), expolygon, chained, min_spacing, params);
            */append(polylines_out, std::move(polylines));
        }
    }
    
    
    // if(((lines.size()>2 && (lines.at(std::floor(lines.size()*.6)).size())<(lines.at(std::floor(lines.size()*.3)).size())))) 
    //     std::reverse(lines.begin(), lines.end());
    /*std::sort(lines.begin(), lines.end(),
              [](const auto &i1, const auto &i2) {
                  return i1.at(0).x() >
                             i2.at(0).x() &&
                         i1.at(0).y() > i2.at(0).y();
              });*/
    //loops = union_pt_chained_outside_in(loops);
    //for (Polygons::const_iterator it = loops.begin(); it != loops.end();
    //        ++it) {
    //    line.clear();
    //    for (const Point &point : it->points) {
    //        pt = point.cast<double>();
    //        // add_offset_line(pt);
    //        pt += Vec2d(0.5 - (pt.x() < 0),
    //                    0.5 - (pt.y() < 0));
    //        line.emplace_back(ClipperLib::cInt(pt.x()),
    //                            ClipperLib::cInt(pt.y()));
    //    }
    //    lines.push_back(line);
    //}
    
    //TODO: return ExtrusionLoop objects to get better chained paths,
    // otherwise the outermost loop starts at the closest point to (0, 0).
    // We want the loops to be split inside the G-code generator to get optimum path planning.
    // FIXME Vojtech: This is only performed for horizontal lines, not for the
    // vertical lines!
    /* const float INFILL_OVERLAP_OVER_SPACING = 0.8f;
    // How much to extend an infill path from expolygon outside?
    coord_t extra = coord_t(
        floor(min_spacing * INFILL_OVERLAP_OVER_SPACING + 0.5f));
    for (Polylines::iterator it_polyline = polylines.begin();
         it_polyline != polylines.end(); ++it_polyline) {
        Point *first_point = &it_polyline->points.front();
        Point *last_point  = &it_polyline->points.back();
        if (first_point->x() > last_point->x())
            std::swap(first_point, last_point);
        first_point->x() -= extra;
        last_point->x() += extra;
        if (first_point->y() > last_point->y())
            std::swap(first_point, last_point);
        first_point->y() -= extra;
        last_point->y() += extra;
    }
    
    }*/
    }
} // namespace Slic3r
