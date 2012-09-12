use Test::More;
use strict;
use warnings;

plan tests => 4;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(scale X Y);
use Slic3r::Surface qw(:types);

sub scale_points (@) { map [scale $_->[X], scale $_->[Y]], @_ }

{
    my $filler = Slic3r::Fill::Rectilinear->new(print => Slic3r::Print->new);
    my $surface_width = 250;
    my $distance = $filler->adjust_solid_spacing(
        width       => $surface_width,
        distance    => 100,
    );
    is $distance, 125, 'adjusted solid distance';
    is $surface_width % $distance, 0, 'adjusted solid distance';
}

{
    my $filler = Slic3r::Fill::Rectilinear->new(
        print               => Slic3r::Print->new,
        max_print_dimension => scale 100,
    );
    my $surface = Slic3r::Surface->new(
        surface_type    => S_TYPE_TOP,
        expolygon       => Slic3r::ExPolygon->new([ scale_points [0,0], [50,0], [50,50], [0,50] ]),
    );
    foreach my $angle (0, 45) {
        $surface->expolygon->rotate($angle, [0,0]);
        my ($params, @paths) = $filler->fill_surface($surface, flow_spacing => 0.69, density => 0.4);
        is scalar @paths, 1, 'one continuous path';
    }
}

__END__
