package Slic3r::Fill::Concentric;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(scale unscale X);
use Slic3r::Geometry::Clipper qw(offset offset2 union_pt traverse_pt);

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # no rotation is supported for this infill pattern
    
    my $expolygon = $surface->expolygon;
    my $bounding_box = $expolygon->bounding_box;
    
    my $min_spacing = scale $params{flow_spacing};
    my $distance = $min_spacing / $params{density};
    
    my $flow_spacing = $params{flow_spacing};
    if ($params{density} == 1 && !$params{dont_adjust}) {
        $distance = $self->adjust_solid_spacing(
            width       => $bounding_box->size->[X],
            distance    => $distance,
        );
        $flow_spacing = unscale $distance;
    }
    
    # compensate the overlap which is good for rectilinear but harmful for concentric
    # where the perimeter/infill spacing should be equal to any other loop spacing
    my @loops = my @last = @{offset(\@$expolygon, -&Slic3r::INFILL_OVERLAP_OVER_SPACING * $min_spacing / 2)};
    while (@last) {
        push @loops, @last = @{offset2(\@last, -1.5*$distance,  +0.5*$distance)};
    }
    
    # generate paths from the outermost to the innermost, to avoid 
    # adhesion problems of the first central tiny loops
    @loops = map Slic3r::Polygon->new(@$_),
        reverse traverse_pt( union_pt(\@loops) );
    
    # order paths using a nearest neighbor search
    my @paths = ();
    my $last_pos = Slic3r::Point->new(0,0);
    foreach my $loop (@loops) {
        push @paths, $loop->split_at_index($last_pos->nearest_point_index(\@$loop));
        $last_pos = $paths[-1]->last_point;
    }
    
    # clip the paths to avoid the extruder to get exactly on the first point of the loop
    my $clip_length = scale $flow_spacing * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_SPACING;
    $_->clip_end($clip_length) for @paths;
    
    # TODO: return ExtrusionLoop objects to get better chained paths
    return { flow_spacing => $flow_spacing, no_sort => 1 }, @paths;
}

1;
