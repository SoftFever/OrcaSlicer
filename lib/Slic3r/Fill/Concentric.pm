package Slic3r::Fill::Concentric;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(scale unscale X1 X2);
use Slic3r::Geometry::Clipper qw(offset2 union_pt traverse_pt PFT_EVENODD);

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # no rotation is supported for this infill pattern
    
    my $expolygon = $surface->expolygon;
    my $bounding_box = [ $expolygon->bounding_box ];
    
    my $min_spacing = scale $params{flow_spacing};
    my $distance = $min_spacing / $params{density};
    
    my $flow_spacing = $params{flow_spacing};
    if ($params{density} == 1 && !$params{dont_adjust}) {
        $distance = $self->adjust_solid_spacing(
            width       => $bounding_box->[X2] - $bounding_box->[X1],
            distance    => $distance,
        );
        $flow_spacing = unscale $distance;
    }
    
    my @loops = my @last = @$expolygon;
    while (@last) {
        push @loops, @last = offset2(\@last, -1.5*$distance,  +0.5*$distance);
    }
    
    # generate paths from the outermost to the innermost, to avoid 
    # adhesion problems of the first central tiny loops
    my @paths = map Slic3r::Polygon->new(@$_)->split_at_first_point,
        reverse traverse_pt( union_pt(\@loops, PFT_EVENODD) );
    
    # clip the paths to avoid the extruder to get exactly on the first point of the loop
    my $clip_length = scale $flow_spacing * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_SPACING;
    $_->clip_end($clip_length) for @paths;
    
    # TODO: return ExtrusionLoop objects to get better chained paths
    return { flow_spacing => $flow_spacing, no_sort => 1 }, @paths;
}

1;
