use Test::More;
use strict;
use warnings;

plan tests => 34;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(scale X Y);
use Slic3r::Geometry::Clipper qw(diff_ex);
use Slic3r::Surface qw(:types);
use Slic3r::Test;

sub scale_points (@) { map [scale $_->[X], scale $_->[Y]], @_ }

{
    my $print = Slic3r::Print->new;
    $print->init_extruders;
    my $filler = Slic3r::Fill::Rectilinear->new(
        print           => $print,
        bounding_box    => Slic3r::Geometry::BoundingBox->new_from_points([ Slic3r::Point->new(0, 0), Slic3r::Point->new(10, 10) ]),
    );
    my $surface_width = 250;
    my $distance = $filler->adjust_solid_spacing(
        width       => $surface_width,
        distance    => 100,
    );
    is $distance, 125, 'adjusted solid distance';
    is $surface_width % $distance, 0, 'adjusted solid distance';
}

{
    my $expolygon = Slic3r::ExPolygon->new([ scale_points [0,0], [50,0], [50,50], [0,50] ]);
    my $filler = Slic3r::Fill::Rectilinear->new(
        bounding_box => $expolygon->bounding_box,
    );
    my $surface = Slic3r::Surface->new(
        surface_type    => S_TYPE_TOP,
        expolygon       => $expolygon,
    );
    foreach my $angle (0, 45) {
        $surface->expolygon->rotate(Slic3r::Geometry::deg2rad($angle), [0,0]);
        my ($params, @paths) = $filler->fill_surface($surface, flow_spacing => 0.69, density => 0.4);
        is scalar @paths, 1, 'one continuous path';
    }
}

{
    my $test = sub {
        my ($expolygon, $flow_spacing, $angle, $density) = @_;
        
        my $filler = Slic3r::Fill::Rectilinear->new(
            bounding_box    => $expolygon->bounding_box,
            angle           => $angle // 0,
        );
        my $surface = Slic3r::Surface->new(
            surface_type    => S_TYPE_BOTTOM,
            expolygon       => $expolygon,
        );
        my ($params, @paths) = $filler->fill_surface(
            $surface,
            flow_spacing    => $flow_spacing,
            density         => $density // 1,
        );
        
        # check whether any part was left uncovered
        my @grown_paths = map Slic3r::Polyline->new(@$_)->grow(scale $params->{flow_spacing}/2), @paths;
        my $uncovered = diff_ex([ @$expolygon ], [ @grown_paths ], 1);
        
        # ignore very small dots
        @$uncovered = grep $_->area > (scale $flow_spacing)**2, @$uncovered;
        
        is scalar(@$uncovered), 0, 'solid surface is fully filled';
        
        if (0 && @$uncovered) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output(
                "uncovered.svg",
                expolygons => [$expolygon],
                red_expolygons => $uncovered,
            );
            exit;
        }
    };
    
    my $expolygon = Slic3r::ExPolygon->new([
        [6883102, 9598327.01296997],
        [6883102, 20327272.01297],
        [3116896, 20327272.01297],
        [3116896, 9598327.01296997],
    ]);
    $test->($expolygon, 0.55);
    
    for (1..20) {
        $expolygon->scale(1.05);
        $test->($expolygon, 0.55);
    }
    
    $expolygon = Slic3r::ExPolygon->new(
        [[59515297,5422499],[59531249,5578697],[59695801,6123186],[59965713,6630228],[60328214,7070685],[60773285,7434379],[61274561,7702115],[61819378,7866770],[62390306,7924789],[62958700,7866744],[63503012,7702244],[64007365,7434357],[64449960,7070398],[64809327,6634999],[65082143,6123325],[65245005,5584454],[65266967,5422499],[66267307,5422499],[66269190,8310081],[66275379,17810072],[66277259,20697500],[65267237,20697500],[65245004,20533538],[65082082,19994444],[64811462,19488579],[64450624,19048208],[64012101,18686514],[63503122,18415781],[62959151,18251378],[62453416,18198442],[62390147,18197355],[62200087,18200576],[61813519,18252990],[61274433,18415918],[60768598,18686517],[60327567,19047892],[59963609,19493297],[59695865,19994587],[59531222,20539379],[59515153,20697500],[58502480,20697500],[58502480,5422499]]
    );
    $test->($expolygon, 0.524341649025257);
    
    $expolygon = Slic3r::ExPolygon->new([ scale_points [0,0], [98,0], [98,10], [0,10] ]);
    $test->($expolygon, 0.5, 45, 0.99);  # non-solid infill
}

{
    my $collection = Slic3r::Polyline::Collection->new(
        Slic3r::Polyline->new([0,15], [0,18], [0,20]),
        Slic3r::Polyline->new([0,10], [0,8], [0,5]),
    );
    is_deeply
        [ map $_->[Y], map @$_, @{$collection->chained_path_from(Slic3r::Point->new(0,30), 0)} ],
        [20, 18, 15, 10, 8, 5],
        'chained path';
}

{
    my $collection = Slic3r::Polyline::Collection->new(
        Slic3r::Polyline->new([4,0], [10,0], [15,0]),
        Slic3r::Polyline->new([10,5], [15,5], [20,5]),
    );
    is_deeply
        [ map $_->[X], map @$_, @{$collection->chained_path_from(Slic3r::Point->new(30,0), 0)} ],
        [reverse 4, 10, 15, 10, 15, 20],
        'chained path';
}

{
    my $collection = Slic3r::ExtrusionPath::Collection->new(
        map Slic3r::ExtrusionPath->new(polyline => $_, role => 0),
            Slic3r::Polyline->new([0,15], [0,18], [0,20]),
            Slic3r::Polyline->new([0,10], [0,8], [0,5]),
    );
    is_deeply
        [ map $_->[Y], map @{$_->polyline}, @{$collection->chained_path_from(Slic3r::Point->new(0,30), 0)} ],
        [20, 18, 15, 10, 8, 5],
        'chained path';
}

{
    my $collection = Slic3r::ExtrusionPath::Collection->new(
        map Slic3r::ExtrusionPath->new(polyline => $_, role => 0),
            Slic3r::Polyline->new([15,0], [10,0], [4,0]),
            Slic3r::Polyline->new([10,5], [15,5], [20,5]),
    );
    is_deeply
        [ map $_->[X], map @{$_->polyline}, @{$collection->chained_path_from(Slic3r::Point->new(30,0), 0)} ],
        [reverse 4, 10, 15, 10, 15, 20],
        'chained path';
}

for my $pattern (qw(hilbertcurve concentric)) {
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('fill_pattern', $pattern);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), "successful $pattern infill generation";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 0);
    $config->set('fill_density', 0);
    $config->set('top_solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    $config->set('solid_infill_below_area', 20000000);
    $config->set('solid_infill_every_layers', 2);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my %layers_with_extrusion = ();
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        $layers_with_extrusion{$self->Z} = 1 if $info->{extruding};
    });
    
    ok !%layers_with_extrusion,
        "solid_infill_below_area and solid_infill_every_layers are ignored when fill_density is 0";
}

__END__
