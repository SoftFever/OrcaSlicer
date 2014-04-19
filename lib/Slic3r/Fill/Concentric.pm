package Slic3r::Fill::Concentric;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(scale unscale X);
use Slic3r::Geometry::Clipper qw(offset offset2 union_pt_chained);

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # no rotation is supported for this infill pattern
    
    my $expolygon = $surface->expolygon;
    my $bounding_box = $expolygon->bounding_box;
    
    my $flow = $params{flow};
    my $min_spacing = $flow->scaled_spacing;
    my $distance = $min_spacing / $params{density};
    
    my $flow_spacing = $flow->spacing;
    if ($params{density} == 1 && !$params{dont_adjust}) {
        $distance = $self->adjust_solid_spacing(
            width       => $bounding_box->size->[X],
            distance    => $distance,
        );
        $flow = Slic3r::Flow->new_from_spacing(
            spacing             => unscale($distance),
            nozzle_diameter     => $flow->nozzle_diameter,
            layer_height        => ($params{layer_height} or die "No layer_height supplied to fill_surface()"),
            bridge              => $flow->bridge,
        );
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
        reverse @{union_pt_chained(\@loops)};
    
    # order paths using a nearest neighbor search
    my @paths = ();
    my $last_pos = Slic3r::Point->new(0,0);
    foreach my $loop (@loops) {
        push @paths, $loop->split_at_index($last_pos->nearest_point_index(\@$loop));
        $last_pos = $paths[-1]->last_point;
    }
    
    # clip the paths to prevent the extruder from getting exactly on the first point of the loop
    my $clip_length = scale($flow->nozzle_diameter) * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER;
    $_->clip_end($clip_length) for @paths;
    @paths = grep $_->is_valid, @paths;  # remove empty paths (too short, thus eaten by clipping)
    
    # TODO: return ExtrusionLoop objects to get better chained paths
    return { flow => $flow, no_sort => 1 }, @paths;
}

1;
