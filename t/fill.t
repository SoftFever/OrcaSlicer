use Test::More;
use strict;
use warnings;

plan tests => 5;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(scale X Y);
use Slic3r::Surface qw(:types);

sub scale_points (@) { map [scale $_->[X], scale $_->[Y]], @_ }

{
    my $print = Slic3r::Print->new;
    $print->init_extruders;
    my $filler = Slic3r::Fill::Rectilinear->new(print => $print);
    my $surface_width = 250;
    my $distance = $filler->adjust_solid_spacing(
        width       => $surface_width,
        distance    => 100,
    );
    is $distance, 125, 'adjusted solid distance';
    is $surface_width % $distance, 0, 'adjusted solid distance';
}

{
    my $filler = Slic3r::Fill::Rectilinear->new;
    my $surface = Slic3r::Surface->new(
        surface_type    => S_TYPE_TOP,
        expolygon       => Slic3r::ExPolygon->new([ scale_points [0,0], [50,0], [50,50], [0,50] ]),
    );
    foreach my $angle (0, 45) {
        $surface->expolygon->rotate(Slic3r::Geometry::deg2rad($angle), [0,0]);
        my ($params, @paths) = $filler->fill_surface($surface, flow_spacing => 0.69, density => 0.4);
        is scalar @paths, 1, 'one continuous path';
    }
}

{
    my $collection = Slic3r::Polyline::Collection->new(polylines => [
        Slic3r::Polyline->new([0,15], [0,18], [0,20]),
        Slic3r::Polyline->new([0,10], [0,8], [0,5]),
    ]);
    is_deeply
        [ map $_->[Y], map @$_, $collection->shortest_path(Slic3r::Point->new(0,30)) ],
        [20, 18, 15, 10, 8, 5],
        'shortest path';
}

__END__
