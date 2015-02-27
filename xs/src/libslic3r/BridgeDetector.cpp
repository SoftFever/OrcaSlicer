#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include <algorithm>

namespace Slic3r {

class BridgeDirectionComparator {
    public:
    std::map<double,double> dir_coverage, dir_avg_length;  // angle => score
    
    BridgeDirectionComparator(double _extrusion_width)
        : extrusion_width(_extrusion_width) {};
    
    // the best direction is the one causing most lines to be bridged (thus most coverage)
    // and shortest max line length
    bool operator() (double a, double b) {
        double coverage_diff = this->dir_coverage[a] - this->dir_coverage[b];
        if (fabs(coverage_diff) < this->extrusion_width) {
            return (this->dir_avg_length[b] > this->dir_avg_length[a]);
        } else {
            return (coverage_diff > 0);
        }
    };
    
    private:
    double extrusion_width;
};

BridgeDetector::BridgeDetector(const ExPolygon &_expolygon, const ExPolygonCollection &_lower_slices,
    coord_t _extrusion_width)
    : expolygon(_expolygon), lower_slices(_lower_slices), extrusion_width(_extrusion_width),
        angle(-1), resolution(PI/36.0)
{
    /*  outset our bridge by an arbitrary amout; we'll use this outer margin
        for detecting anchors */
    Polygons grown;
    offset((Polygons)this->expolygon, &grown, this->extrusion_width);
    
    // detect what edges lie on lower slices by turning bridge contour and holes
    // into polylines and then clipping them with each lower slice's contour
    intersection(grown, this->lower_slices.contours(), &this->_edges);
    
    #ifdef SLIC3R_DEBUG
    printf("  bridge has %zu support(s)\n", this->_edges.size());
    #endif
    
    // detect anchors as intersection between our bridge expolygon and the lower slices
    // safety offset required to avoid Clipper from detecting empty intersection while Boost actually found some edges
    intersection(grown, this->lower_slices, &this->_anchors, true);
    
    /*
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("bridge.svg",
            expolygons      => [ $self->expolygon ],
            red_expolygons  => $self->lower_slices,
            polylines       => $self->_edges,
        );
    }
    */
}

bool
BridgeDetector::detect_angle()
{
    if (this->_edges.empty() || this->_anchors.empty()) return false;
    
    /*  Outset the bridge expolygon by half the amount we used for detecting anchors;
        we'll use this one to clip our test lines and be sure that their endpoints
        are inside the anchors and not on their contours leading to false negatives. */
    Polygons clip_area;
    offset(this->expolygon, &clip_area, +this->extrusion_width/2);
    
    /*  we'll now try several directions using a rudimentary visibility check:
        bridge in several directions and then sum the length of lines having both
        endpoints within anchors */
    
    // we test angles according to configured resolution
    std::vector<double> angles;
    for (int i = 0; i <= PI/this->resolution; ++i)
        angles.push_back(i * this->resolution);
    
    // we also test angles of each bridge contour
    {
        Polygons pp = this->expolygon;
        for (Polygons::const_iterator p = pp.begin(); p != pp.end(); ++p) {
            Lines lines = p->lines();
            for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line)
                angles.push_back(line->direction());
        }
    }
    
    /*  we also test angles of each open supporting edge
        (this finds the optimal angle for C-shaped supports) */
    for (Polylines::const_iterator edge = this->_edges.begin(); edge != this->_edges.end(); ++edge) {
        if (edge->first_point().coincides_with(edge->last_point())) continue;
        angles.push_back(Line(edge->first_point(), edge->last_point()).direction());
    }
    
    // remove duplicates
    double min_resolution = PI/180.0;  // 1 degree
    std::sort(angles.begin(), angles.end());
    for (size_t i = 1; i < angles.size(); ++i) {
        if (Slic3r::Geometry::directions_parallel(angles[i], angles[i-1], min_resolution)) {
            angles.erase(angles.begin() + i);
            --i;
        }
    }
    /*  compare first value with last one and remove the greatest one (PI) 
        in case they are parallel (PI, 0) */
    if (Slic3r::Geometry::directions_parallel(angles.front(), angles.back(), min_resolution))
        angles.pop_back();
    
    BridgeDirectionComparator bdcomp(this->extrusion_width);
    double line_increment = this->extrusion_width;
    bool have_coverage = false;
    for (std::vector<double>::const_iterator angle = angles.begin(); angle != angles.end(); ++angle) {
        Polygons my_clip_area = clip_area;
        ExPolygons my_anchors = this->_anchors;
        
        // rotate everything - the center point doesn't matter
        for (Polygons::iterator it = my_clip_area.begin(); it != my_clip_area.end(); ++it)
            it->rotate(-*angle, Point(0,0));
        for (ExPolygons::iterator it = my_anchors.begin(); it != my_anchors.end(); ++it)
            it->rotate(-*angle, Point(0,0));
    
        // generate lines in this direction
        BoundingBox bb;
        for (ExPolygons::const_iterator it = my_anchors.begin(); it != my_anchors.end(); ++it)
            bb.merge((Points)*it);
        
        Lines lines;
        for (coord_t y = bb.min.y; y <= bb.max.y; y += line_increment)
            lines.push_back(Line(Point(bb.min.x, y), Point(bb.max.x, y)));
        
        Lines clipped_lines;
        intersection(lines, my_clip_area, &clipped_lines);
        
        // remove any line not having both endpoints within anchors
        for (size_t i = 0; i < clipped_lines.size(); ++i) {
            Line &line = clipped_lines[i];
            if (!Slic3r::Geometry::contains(my_anchors, line.a)
                || !Slic3r::Geometry::contains(my_anchors, line.b)) {
                clipped_lines.erase(clipped_lines.begin() + i);
                --i;
            }
        }
        
        std::vector<double> lengths;
        double total_length = 0;
        for (Lines::const_iterator line = clipped_lines.begin(); line != clipped_lines.end(); ++line) {
            double len = line->length();
            lengths.push_back(len);
            total_length += len;
        }
        if (total_length) have_coverage = true;
        
        // sum length of bridged lines
        bdcomp.dir_coverage[*angle] = total_length;
        
        /*  The following produces more correct results in some cases and more broken in others.
            TODO: investigate, as it looks more reliable than line clipping. */
        // $directions_coverage{$angle} = sum(map $_->area, @{$self->coverage($angle)}) // 0;
        
        // max length of bridged lines
        bdcomp.dir_avg_length[*angle] = !lengths.empty()
            ? *std::max_element(lengths.begin(), lengths.end())
            : 0;
    }
    
    // if no direction produced coverage, then there's no bridge direction
    if (!have_coverage) return false;
    
    // sort directions by score
    std::sort(angles.begin(), angles.end(), bdcomp);
    
    this->angle = angles.front();
    if (this->angle >= PI) this->angle -= PI;
    
    #ifdef SLIC3R_DEBUG
    printf("  Optimal infill angle is %d degrees\n", (int)Slic3r::Geometry::rad2deg(this->angle));
    #endif
    
    return true;
}

void
BridgeDetector::coverage(Polygons* coverage) const
{
    if (this->angle == -1) return;
    return this->coverage(angle, coverage);
}

void
BridgeDetector::coverage(double angle, Polygons* coverage) const
{
    // Clone our expolygon and rotate it so that we work with vertical lines.
    ExPolygon expolygon = this->expolygon;
    expolygon.rotate(PI/2.0 - angle, Point(0,0));
    
    /*  Outset the bridge expolygon by half the amount we used for detecting anchors;
        we'll use this one to generate our trapezoids and be sure that their vertices
        are inside the anchors and not on their contours leading to false negatives. */
    ExPolygons grown;
    offset(expolygon, &grown, this->extrusion_width/2.0);
    
    // Compute trapezoids according to a vertical orientation
    Polygons trapezoids;
    for (ExPolygons::const_iterator it = grown.begin(); it != grown.end(); ++it)
        it->get_trapezoids2(&trapezoids, PI/2.0);
    
    // get anchors, convert them to Polygons and rotate them too
    Polygons anchors;
    for (ExPolygons::const_iterator anchor = this->_anchors.begin(); anchor != this->_anchors.end(); ++anchor) {
        Polygons pp = *anchor;
        for (Polygons::iterator p = pp.begin(); p != pp.end(); ++p)
            p->rotate(PI/2.0 - angle, Point(0,0));
        anchors.insert(anchors.end(), pp.begin(), pp.end());
    }
    
    Polygons covered;
    for (Polygons::const_iterator trapezoid = trapezoids.begin(); trapezoid != trapezoids.end(); ++trapezoid) {
        Lines lines = trapezoid->lines();
        Lines supported;
        intersection(lines, anchors, &supported);
        
        // not nice, we need a more robust non-numeric check
        for (size_t i = 0; i < supported.size(); ++i) {
            if (supported[i].length() < this->extrusion_width) {
                supported.erase(supported.begin() + i);
                i--;
            }
        }

        if (supported.size() >= 2) covered.push_back(*trapezoid);        
    }
    
    // merge trapezoids and rotate them back
    Polygons _coverage;
    union_(covered, &_coverage);
    for (Polygons::iterator p = _coverage.begin(); p != _coverage.end(); ++p)
        p->rotate(-(PI/2.0 - angle), Point(0,0));
    
    // intersect trapezoids with actual bridge area to remove extra margins
    // and append it to result
    intersection(_coverage, this->expolygon, coverage);
    
    /*
    if (0) {
        my @lines = map @{$_->lines}, @$trapezoids;
        $_->rotate(-(PI/2 - $angle), [0,0]) for @lines;
        
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(
            "coverage_" . rad2deg($angle) . ".svg",
            expolygons          => [$self->expolygon],
            green_expolygons    => $self->_anchors,
            red_expolygons      => $coverage,
            lines               => \@lines,
        );
    }
    */
}

/*  This method returns the bridge edges (as polylines) that are not supported
    but would allow the entire bridge area to be bridged with detected angle
    if supported too */
void
BridgeDetector::unsupported_edges(Polylines* unsupported) const
{
    if (this->angle == -1) return;
    return this->unsupported_edges(this->angle, unsupported);
}

void
BridgeDetector::unsupported_edges(double angle, Polylines* unsupported) const
{
    // get bridge edges (both contour and holes)
    Polylines bridge_edges;
    {
        Polygons pp = this->expolygon;
        bridge_edges.insert(bridge_edges.end(), pp.begin(), pp.end());  // this uses split_at_first_point()
    }
    
    // get unsupported edges
    Polygons grown_lower;
    offset(this->lower_slices, &grown_lower, +this->extrusion_width);
    Polylines _unsupported;
    diff(bridge_edges, grown_lower, &_unsupported);
    
    /*  Split into individual segments and filter out edges parallel to the bridging angle
        TODO: angle tolerance should probably be based on segment length and flow width,
        so that we build supports whenever there's a chance that at least one or two bridge
        extrusions would be anchored within such length (i.e. a slightly non-parallel bridging
        direction might still benefit from anchors if long enough) */
    double angle_tolerance = PI / 180.0 * 5.0;
    for (Polylines::const_iterator polyline = _unsupported.begin(); polyline != _unsupported.end(); ++polyline) {
        Lines lines = polyline->lines();
        for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
            if (!Slic3r::Geometry::directions_parallel(line->direction(), angle))
                unsupported->push_back(*line);
        }
    }
    
    /*
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(
            "unsupported_" . rad2deg($angle) . ".svg",
            expolygons          => [$self->expolygon],
            green_expolygons    => $self->_anchors,
            red_expolygons      => union_ex($grown_lower),
            no_arrows           => 1,
            polylines           => \@bridge_edges,
            red_polylines       => $unsupported,
        );
    }
    */
}

#ifdef SLIC3RXS
REGISTER_CLASS(BridgeDetector, "BridgeDetector");
#endif

}
