use Test::More;
use strict;
use warnings;

plan tests => 43;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first sum);
use Slic3r;
use Slic3r::Geometry qw(X Y scale unscale convex_hull);
use Slic3r::Geometry::Clipper qw(union diff diff_ex offset offset2_ex);
use Slic3r::Surface qw(:types);
use Slic3r::Test;

sub scale_points (@) { map [scale $_->[X], scale $_->[Y]], @_ }

{
    my $print = Slic3r::Print->new;
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
        angle        => 0,
    );
    my $surface = Slic3r::Surface->new(
        surface_type    => S_TYPE_TOP,
        expolygon       => $expolygon,
    );
    my $flow = Slic3r::Flow->new(
        width           => 0.69,
        height          => 0.4,
        nozzle_diameter => 0.50,
    );
    $filler->spacing($flow->spacing);
    foreach my $angle (0, 45) {
        $surface->expolygon->rotate(Slic3r::Geometry::deg2rad($angle), [0,0]);
        my @paths = $filler->fill_surface($surface, layer_height => 0.4, density => 0.4);
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
        my $flow = Slic3r::Flow->new(
            width           => $flow_spacing,
            height          => 0.4,
            nozzle_diameter => $flow_spacing,
        );
        $filler->spacing($flow->spacing);
        my @paths = $filler->fill_surface(
            $surface,
            layer_height    => $flow->height,
            density         => $density // 1,
        );
        
        # check whether any part was left uncovered
        my @grown_paths = map @{Slic3r::Polyline->new(@$_)->grow(scale $filler->spacing/2)}, @paths;
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
        map Slic3r::ExtrusionPath->new(polyline => $_, role => 0, mm3_per_mm => 1),
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
        map Slic3r::ExtrusionPath->new(polyline => $_, role => 0, mm3_per_mm => 1),
            Slic3r::Polyline->new([15,0], [10,0], [4,0]),
            Slic3r::Polyline->new([10,5], [15,5], [20,5]),
    );
    is_deeply
        [ map $_->[X], map @{$_->polyline}, @{$collection->chained_path_from(Slic3r::Point->new(30,0), 0)} ],
        [reverse 4, 10, 15, 10, 15, 20],
        'chained path';
}

for my $pattern (qw(rectilinear honeycomb hilbertcurve concentric)) {
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('fill_pattern', $pattern);
    $config->set('external_fill_pattern', $pattern);
    $config->set('perimeters', 1);
    $config->set('skirts', 0);
    $config->set('fill_density', 20);
    $config->set('layer_height', 0.05);
    $config->set('perimeter_extruder', 1);
    $config->set('infill_extruder', 2);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config, scale => 2);
    ok my $gcode = Slic3r::Test::gcode($print), "successful $pattern infill generation";
    my $tool = undef;
    my @perimeter_points = my @infill_points = ();
    Slic3r::GCode::Reader->new->parse($gcode, sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
        } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            if ($tool == $config->perimeter_extruder-1) {
                push @perimeter_points, Slic3r::Point->new_scale($args->{X}, $args->{Y});
            } elsif ($tool == $config->infill_extruder-1) {
                push @infill_points, Slic3r::Point->new_scale($args->{X}, $args->{Y});
            }
        }
    });
    my $convex_hull = convex_hull(\@perimeter_points);
    ok !(defined first { !$convex_hull->contains_point($_) } @infill_points), "infill does not exceed perimeters ($pattern)";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('infill_only_where_needed', 1);
    $config->set('bottom_solid_layers', 0);
    $config->set('infill_extruder', 2);
    $config->set('infill_extrusion_width', 0.5);
    $config->set('fill_density', 40);
    $config->set('cooling', 0);                 # for preventing speeds from being altered
    $config->set('first_layer_speed', '100%');  # for preventing speeds from being altered
    
    my $test = sub {
        my $print = Slic3r::Test::init_print('pyramid', config => $config);
        
        my $tool = undef;
        my @infill_extrusions = ();  # array of polylines
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($cmd =~ /^T(\d+)/) {
                $tool = $1;
            } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
                if ($tool == $config->infill_extruder-1) {
                    push @infill_extrusions, Slic3r::Line->new_scale(
                        [ $self->X, $self->Y ],
                        [ $info->{new_X}, $info->{new_Y} ],
                    );
                }
            }
        });
        return 0 if !@infill_extrusions;  # prevent calling convex_hull() with no points
        
        my $convex_hull = convex_hull([ map $_->pp, map @$_, @infill_extrusions ]);
        return unscale unscale sum(map $_->area, @{offset([$convex_hull], scale(+$config->infill_extrusion_width/2))});
    };
    
    my $tolerance = 5;  # mm^2
    
    $config->set('solid_infill_below_area', 0);
    ok $test->() < $tolerance,
        'no infill is generated when using infill_only_where_needed on a pyramid';
    
    $config->set('solid_infill_below_area', 70);
    ok abs($test->() - $config->solid_infill_below_area) < $tolerance,
        'infill is only generated under the forced solid shells';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 1);
    $config->set('fill_density', 0);
    $config->set('top_solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    $config->set('solid_infill_below_area', 20000000);
    $config->set('solid_infill_every_layers', 2);
    $config->set('perimeter_speed', 99);
    $config->set('external_perimeter_speed', 99);
    $config->set('cooling', 0);
    $config->set('first_layer_speed', '100%');
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my %layers_with_extrusion = ();
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && $info->{dist_XY} > 0 && $info->{extruding}) {
            if (($args->{F} // $self->F) != $config->perimeter_speed*60) {
                $layers_with_extrusion{$self->Z} = ($args->{F} // $self->F);
            }
        }
    });
    
    ok !%layers_with_extrusion,
        "solid_infill_below_area and solid_infill_every_layers are ignored when fill_density is 0";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 3);
    $config->set('fill_density', 0);
    $config->set('layer_height', 0.2);
    $config->set('first_layer_height', 0.2);
    $config->set('nozzle_diameter', [0.35]);
    $config->set('infill_extruder', 2);
    $config->set('solid_infill_extruder', 2);
    $config->set('infill_extrusion_width', 0.52);
    $config->set('solid_infill_extrusion_width', 0.52);
    $config->set('first_layer_extrusion_width', 0);
    
    my $print = Slic3r::Test::init_print('A', config => $config);
    my %infill = ();  # Z => [ Line, Line ... ]
    my $tool = undef;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
        } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            if ($tool == $config->infill_extruder-1) {
                my $z = 1 * $self->Z;
                $infill{$z} ||= [];
                push @{$infill{$z}}, Slic3r::Line->new_scale(
                    [ $self->X, $self->Y ],
                    [ $info->{new_X}, $info->{new_Y} ],
                );
            }
        }
    });
    my $grow_d = scale($config->infill_extrusion_width)/2;
    my $layer0_infill = union([ map @{$_->grow($grow_d)}, @{ $infill{0.2} } ]);
    my $layer1_infill = union([ map @{$_->grow($grow_d)}, @{ $infill{0.4} } ]);
    my $diff = diff($layer0_infill, $layer1_infill);
    $diff = offset2_ex($diff, -$grow_d, +$grow_d);
    $diff = [ grep { $_->area > 2*(($grow_d*2)**2) } @$diff ];
    is scalar(@$diff), 0, 'no missing parts in solid shell when fill_density is 0';
}

__END__
