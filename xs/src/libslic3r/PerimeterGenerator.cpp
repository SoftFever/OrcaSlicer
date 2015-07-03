#include "PerimeterGenerator.hpp"

namespace Slic3r {

void
PerimeterGenerator::process()
{

}

ExtrusionEntityCollection
PerimeterGenerator::_traverse_loops(const std::vector<PerimeterGeneratorLoop> &loops,
    const Polylines &thin_walls) const
{
    
}

ExtrusionEntityCollection
PerimeterGenerator::_fill_gaps(double min, double max, double w,
    const Polygons &gaps) const
{
    ExtrusionEntityCollection coll;
    
    min *= (1 - INSET_OVERLAP_TOLERANCE);
    
    ExPolygon curr = diff(
        offset2(gaps, -min/2, +min/2),
        offset2(gaps, -max/2, +max/2),
        true,
    );
    
    Polylines polylines;
    for (ExPolygons::const_iterator ex = curr.begin(); ex != curr.end(); ++ex)
        ex->medial_axis(max, min/2, &polylines);
    if (polylines.empty())
        return coll;
    
    #ifdef SLIC3R_DEBUG
    if (!curr.empty())
        printf("  %d gaps filled with extrusion width = %zu\n", curr.size(), w);
    #endif
    
    //my $flow = $layerm->flow(FLOW_ROLE_SOLID_INFILL, 0, $w);
    Flow flow(
        w, this->layer_height, this->solid_infill_flow.nozzle_diameter
    );
    
    double mm3_per_mm = flow.mm3_per_mm();
    
    /*
    my %path_args = (
        role        => EXTR_ROLE_GAPFILL,
        mm3_per_mm  => $flow->mm3_per_mm,
        width       => $flow->width,
        height      => $self->layer_height,
    );
    */
    
    for (Polylines::const_iterator p = polylines.begin(); p != polylines.end(); ++p) {
        /*
        #if ($polylines[$i]->isa('Slic3r::Polygon')) {
        #    my $loop = Slic3r::ExtrusionLoop->new;
        #    $loop->append(Slic3r::ExtrusionPath->new(polyline => $polylines[$i]->split_at_first_point, %path_args));
        #    $polylines[$i] = $loop;
        */
        if (p->is_valid() && p->first_point().coincides_with(p->last_point())) {
            // since medial_axis() now returns only Polyline objects, detect loops here
            
            
            ExtrusionLoop loop;
            loop.paths.push_back();
        } else {
            
        }
    }
    
    foreach my $polyline (@polylines) {
        #if ($polylines[$i]->isa('Slic3r::Polygon')) {
        #    my $loop = Slic3r::ExtrusionLoop->new;
        #    $loop->append(Slic3r::ExtrusionPath->new(polyline => $polylines[$i]->split_at_first_point, %path_args));
        #    $polylines[$i] = $loop;
        if ($polyline->is_valid && $polyline->first_point->coincides_with($polyline->last_point)) {
            # since medial_axis() now returns only Polyline objects, detect loops here
            push @entities, my $loop = Slic3r::ExtrusionLoop->new;
            $loop->append(Slic3r::ExtrusionPath->new(polyline => $polyline, %path_args));
        } else {
            push @entities, Slic3r::ExtrusionPath->new(polyline => $polyline, %path_args);
        }
    }
    
    return coll;
}

#ifdef SLIC3RXS
REGISTER_CLASS(PerimeterGenerator, "Layer::PerimeterGenerator");
#endif

bool
PerimeterGeneratorLoop::is_external() const
{
    return this->depth == 0;
}

bool
PerimeterGeneratorLoop::is_internal_contour() const
{
    if (this->is_contour) {
        // an internal contour is a contour containing no other contours
        for (std::vector<PerimeterGeneratorLoop>::const_iterator loop = this->children.begin();
            loop != this->children.end(); ++loop) {
            if (loop->is_contour) {
                return false;
            }
        }
        return true;
    }
    return false;
}

}
