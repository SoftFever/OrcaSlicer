package Slic3r::Fill::Concentric;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(scale unscale X);
use Slic3r::Geometry::Clipper qw(offset offset2 union_pt_chained);

sub no_sort { 1 }

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # no rotation is supported for this infill pattern
    
    my $expolygon = $surface->expolygon;
    my $bounding_box = $expolygon->bounding_box;
    
    my $min_spacing = scale($self->spacing);
    my $distance = $min_spacing / $params{density};
    
    if ($params{density} == 1 && !$params{dont_adjust}) {
        $distance = $self->adjust_solid_spacing(
            width       => $bounding_box->size->[X],
            distance    => $distance,
        );
        $self->spacing(unscale $distance);
    }
    
    my @loops = my @last = map $_->clone, @$expolygon;
    while (@last) {
        push @loops, @last = @{offset2(\@last, -($distance + 0.5*$min_spacing), +0.5*$min_spacing)};
    }
    
    # generate paths from the outermost to the innermost, to avoid 
    # adhesion problems of the first central tiny loops
    @loops = map Slic3r::Polygon->new(@$_),
        reverse @{union_pt_chained(\@loops)};
    
    # split paths using a nearest neighbor search
    my @paths = ();
    my $last_pos = Slic3r::Point->new(0,0);
    foreach my $loop (@loops) {
        push @paths, $loop->split_at_index($last_pos->nearest_point_index(\@$loop));
        $last_pos = $paths[-1]->last_point;
    }
    
    # clip the paths to prevent the extruder from getting exactly on the first point of the loop
    $_->clip_end($self->loop_clipping) for @paths;
    @paths = grep $_->is_valid, @paths;  # remove empty paths (too short, thus eaten by clipping)
    
    # TODO: return ExtrusionLoop objects to get better chained paths
    return @paths;
}

1;
