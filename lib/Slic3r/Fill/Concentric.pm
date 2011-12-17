package Slic3r::Fill::Concentric;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(scale X1 Y1 X2 Y2);
use XXX;

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # no rotation is supported for this infill pattern
    
    my $bounding_box = [ $surface->expolygon->bounding_box ];
    
    my $scaled_flow_spacing = scale $params{flow_spacing};
    my $distance = $scaled_flow_spacing / $params{density};
    # TODO: adjust distance and flow width for solid surfaces
    # using the same logic as Rectilinear infill
    # (factor it out to parent class)
    
    my @contour_loops = ();
    my @hole_loops = ();
    my @last_offsets = ($surface->expolygon->offset_ex($distance));
    while (@last_offsets) {
        my @new_offsets = ();
        foreach my $expolygon (@last_offsets) {
            my @offsets = $expolygon->offset_ex(-$distance);
            foreach my $offset (@offsets) {
                push @new_offsets, $offset;
                push @contour_loops, $offset->contour;
                push @hole_loops, $offset->holes;
            }
        }
        @last_offsets = @new_offsets;
    }
    
    my @loops = (@contour_loops, reverse @hole_loops);
    
    # make paths
    my @paths = ();
    my $cur_pos = Slic3r::Point->new(
        ($bounding_box->[X1] + $bounding_box->[X2]) / 2,
        ($bounding_box->[Y1] + $bounding_box->[Y2]) / 2,
    );
    foreach my $loop (map Slic3r::ExtrusionLoop->cast($_, role => 'fill'), @loops) {
        # find the point of the loop that is closest to the current extruder position
        $cur_pos = $loop->nearest_point_to($cur_pos);
        
        # split the loop at the starting point and make a path
        my $path = $loop->split_at($cur_pos);
        
        # clip the path to avoid the extruder to get exactly on the first point of the loop
        $path->clip_end(scale $Slic3r::nozzle_diameter / 2);
        
        push @paths, $path->p if @{$path->points};
    }
    
    return {}, @paths;
}

1;
