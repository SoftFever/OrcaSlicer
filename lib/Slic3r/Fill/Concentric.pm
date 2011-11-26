package Slic3r::Fill::Concentric;
use Moo;

extends 'Slic3r::Fill::Base';

use XXX;

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # no rotation is supported for this infill pattern
    
    my $flow_width_res = $params{flow_width} / $Slic3r::resolution;
    my $distance = $flow_width_res / $params{density};
    
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
    my $cur_pos = Slic3r::Point->new(0,0);
    foreach my $loop (map Slic3r::ExtrusionLoop->cast($_), @loops) {
        # find the point of the loop that is closest to the current extruder position
        $cur_pos = $loop->nearest_point_to($cur_pos);
        
        # split the loop at the starting point and make a path
        my $path = $loop->split_at($cur_pos);
    
        # clip the path to avoid the extruder to get exactly on the first point of the loop
        $path->clip_end($Slic3r::flow_width / $Slic3r::resolution);
        
        push @paths, $path->p;
    }
    
    return @paths;
}

1;
