use Test::More;
use strict;
use warnings;

plan tests => 9;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(scale X Y);
use Slic3r::Surface qw(:types);
use Slic3r::Test;

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
        [ map $_->[Y], map @$_, $collection->chained_path(Slic3r::Point->new(0,30)) ],
        [20, 18, 15, 10, 8, 5],
        'chained path';
}

{
    my $collection = Slic3r::Polyline::Collection->new(polylines => [
        Slic3r::Polyline->new([4,0], [10,0], [15,0]),
        Slic3r::Polyline->new([10,5], [15,5], [20,5]),
    ]);
    is_deeply
        [ map $_->[X], map @$_, $collection->chained_path(Slic3r::Point->new(30,0)) ],
        [reverse 4, 10, 15, 10, 15, 20],
        'chained path';
}

{
    my $collection = Slic3r::ExtrusionPath::Collection->new(paths => [
        map Slic3r::ExtrusionPath->pack(polyline => $_, role => 0),
            Slic3r::Polyline->new([0,15], [0,18], [0,20]),
            Slic3r::Polyline->new([0,10], [0,8], [0,5]),
    ]);
    is_deeply
        [ map $_->[Y], map @{$_->unpack->polyline}, $collection->chained_path(Slic3r::Point->new(0,30)) ],
        [20, 18, 15, 10, 8, 5],
        'chained path';
}

{
    my $collection = Slic3r::ExtrusionPath::Collection->new(paths => [
        map Slic3r::ExtrusionPath->pack(polyline => $_, role => 0),
            Slic3r::Polyline->new([15,0], [10,0], [4,0]),
            Slic3r::Polyline->new([10,5], [15,5], [20,5]),
    ]);
    is_deeply
        [ map $_->[X], map @{$_->unpack->polyline}, $collection->chained_path(Slic3r::Point->new(30,0)) ],
        [reverse 4, 10, 15, 10, 15, 20],
        'chained path';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('fill_pattern', 'hilbertcurve');
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'successful hilbertcurve infill generation';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 0);
    $config->set('fill_density', 0);
    $config->set('top_solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    $config->set('solid_infill_below_area', 20000000);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    Slic3r::Test::GCodeReader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
        my ($self, $cmd, $args, $info) = @_;
        
        fail "solid_infill_below_area should be ignored when fill_density is 0"
            if $info->{extruding};
    });
}

__END__
