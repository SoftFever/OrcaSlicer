use Test::More tests => 23;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use Slic3r;
use List::Util qw(first sum none);
use Slic3r::Geometry qw(epsilon scale unscale scaled_epsilon Y);
use Slic3r::Test;

# Disable this until a more robust implementation is provided. It currently
# fails on Linux 32bit because some spurious extrudates are generated.
if (0) {
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('layer_height', 0.2);
    $config->set('first_layer_height', '100%');
    $config->set('extrusion_width', 0.5);
    $config->set('first_layer_extrusion_width', '200%'); # check this one too
    $config->set('skirts', 0);
    $config->set('thin_walls', 1);
    
    my $print = Slic3r::Test::init_print('gt2_teeth', config => $config);
    
    my %extrusion_paths = ();  # Z => count of continuous extrusions
    my $extruding = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1') {
            if ($info->{extruding} && $info->{dist_XY}) {
                if (!$extruding) {
                    $extrusion_paths{$self->Z} //= 0;
                    $extrusion_paths{$self->Z}++;
                }
                $extruding = 1;
            } else {
                $extruding = 0;
            }
        }
    });
    
    ok !(first { $_ != 3 } values %extrusion_paths),
        'no superfluous thin walls are generated for toothed profile';
}

{
    my $square = Slic3r::Polygon->new_scale(  # ccw
        [100, 100],
        [200, 100],
        [200, 200],
        [100, 200],
    );
    my $hole_in_square = Slic3r::Polygon->new_scale(  # cw
        [140, 140],
        [140, 160],
        [160, 160],
        [160, 140],
    );
    my $expolygon = Slic3r::ExPolygon->new($square, $hole_in_square);
    my $res = $expolygon->medial_axis(scale 40, scale 0.5);
    is scalar(@$res), 1, 'medial axis of a square shape is a single path';
    isa_ok $res->[0], 'Slic3r::Polyline', 'medial axis result is a polyline';
    ok $res->[0]->first_point->coincides_with($res->[0]->last_point), 'polyline forms a closed loop';
    ok $res->[0]->length > $hole_in_square->length && $res->[0]->length < $square->length,
        'medial axis loop has reasonable length';
}

{
    my $expolygon = Slic3r::ExPolygon->new(Slic3r::Polygon->new_scale(
        [100, 100],
        [120, 100],
        [120, 200],
        [100, 200],
    ));
    my $res = $expolygon->medial_axis(scale 20, scale 0.5);
    is scalar(@$res), 1, 'medial axis of a narrow rectangle is a single line';
    ok unscale($res->[0]->length) >= (200-100 - (120-100)) - epsilon, 'medial axis has reasonable length';
    
    $expolygon = Slic3r::ExPolygon->new(Slic3r::Polygon->new_scale(
        [100, 100],
        [120, 100],
        [120, 200],
        [105, 200],  # extra point in the short side
        [100, 200],
    ));
    my $res2 = $expolygon->medial_axis(scale 1, scale 0.5);
    is scalar(@$res), 1, 'medial axis of a narrow rectangle with an extra vertex is still a single line';
    ok unscale($res->[0]->length) >= (200-100 - (120-100)) - epsilon, 'medial axis has still a reasonable length';
    ok !(grep { abs($_ - scale 150) < scaled_epsilon } map $_->[Y], map @$_, @$res2), "extra vertices don't influence medial axis";
}

{
    my $expolygon = Slic3r::ExPolygon->new(
        Slic3r::Polygon->new([1185881,829367],[1421988,1578184],[1722442,2303558],[2084981,2999998],[2506843,3662186],[2984809,4285086],[3515250,4863959],[4094122,5394400],[4717018,5872368],[5379210,6294226],[6075653,6656769],[6801033,6957229],[7549842,7193328],[8316383,7363266],[9094809,7465751],[9879211,7500000],[10663611,7465750],[11442038,7363265],[12208580,7193327],[12957389,6957228],[13682769,6656768],[14379209,6294227],[15041405,5872366],[15664297,5394401],[16243171,4863960],[16758641,4301424],[17251579,3662185],[17673439,3000000],[18035980,2303556],[18336441,1578177],[18572539,829368],[18750748,0],[19758422,0],[19727293,236479],[19538467,1088188],[19276136,1920196],[18942292,2726179],[18539460,3499999],[18070731,4235755],[17539650,4927877],[16950279,5571067],[16307090,6160437],[15614974,6691519],[14879209,7160248],[14105392,7563079],[13299407,7896927],[12467399,8159255],[11615691,8348082],[10750769,8461952],[9879211,8500000],[9007652,8461952],[8142729,8348082],[7291022,8159255],[6459015,7896927],[5653029,7563079],[4879210,7160247],[4143447,6691519],[3451331,6160437],[2808141,5571066],[2218773,4927878],[1687689,4235755],[1218962,3499999],[827499,2748020],[482284,1920196],[219954,1088186],[31126,236479],[0,0],[1005754,0]),
    );
    my $res = $expolygon->medial_axis(scale 1.324888, scale 0.25);
    is scalar(@$res), 1, 'medial axis of a semicircumference is a single line';
    
    # check whether turns are all CCW or all CW
    my @lines = @{$res->[0]->lines};
    my @angles = map { $lines[$_-1]->ccw($lines[$_]->b) } 1..$#lines;
    ok !!(none { $_ < 0 } @angles) || (none { $_ > 0 } @angles),
        'all medial axis segments of a semicircumference have the same orientation';
}

{
    my $expolygon = Slic3r::ExPolygon->new(Slic3r::Polygon->new_scale(
        [100, 100],
        [120, 100],
        [112, 200],
        [108, 200],
    ));
    my $res = $expolygon->medial_axis(scale 20, scale 0.5);
    is scalar(@$res), 1, 'medial axis of a narrow trapezoid is a single line';
    ok unscale($res->[0]->length) >= (200-100 - (120-100)) - epsilon, 'medial axis has reasonable length';
}

{
    my $expolygon = Slic3r::ExPolygon->new(Slic3r::Polygon->new_scale(
        [100, 100],
        [120, 100],
        [120, 180],
        [200, 180],
        [200, 200],
        [100, 200],
    ));
    my $res = $expolygon->medial_axis(scale 20, scale 0.5);
    is scalar(@$res), 1, 'medial axis of a L shape is a single polyline';
    my $len = unscale($res->[0]->length) + 20;  # 20 is the thickness of the expolygon, which is subtracted from the ends
    ok $len > 80*2 && $len < 100*2, 'medial axis has reasonable length';
}

{
    my $expolygon = Slic3r::ExPolygon->new(Slic3r::Polygon->new(
        [-203064906,-51459966],[-219312231,-51459966],[-219335477,-51459962],[-219376095,-51459962],[-219412047,-51459966],
        [-219572094,-51459966],[-219624814,-51459962],[-219642183,-51459962],[-219656665,-51459966],[-220815482,-51459966],
        [-220815482,-37738966],[-221117540,-37738966],[-221117540,-51762024],[-203064906,-51762024],
    ));
    my $polylines = $expolygon->medial_axis(819998, 102499.75);
    
    my $perimeter = $expolygon->contour->split_at_first_point->length;
    ok sum(map $_->length, @$polylines) > $perimeter/2/4*3, 'medial axis has a reasonable length';
}

{
    my $expolygon = Slic3r::ExPolygon->new(Slic3r::Polygon->new_scale(
        [50, 100],
        [1000, 102],
        [50, 104],
    ));
    my $res = $expolygon->medial_axis(scale 4, scale 0.5);
    is scalar(@$res), 1, 'medial axis of a narrow triangle is a single line';
    ok unscale($res->[0]->length) >= (200-100 - (120-100)) - epsilon, 'medial axis has reasonable length';
}

{
    # GH #2474
    my $expolygon = Slic3r::ExPolygon->new(Slic3r::Polygon->new(
        [91294454,31032190],[11294481,31032190],[11294481,29967810],[44969182,29967810],[89909960,29967808],[91294454,29967808]
    ));
    my $polylines = $expolygon->medial_axis(1871238, 500000);
    is scalar(@$polylines), 1, 'medial axis is a single polyline';
    my $polyline = $polylines->[0];
    
    my $expected_y = $expolygon->bounding_box->center->y; #;;
    ok abs(sum(map $_->y, @$polyline) / @$polyline - $expected_y) < scaled_epsilon, #,,
        'medial axis is horizontal and is centered';
    
    # order polyline from left to right
    $polyline->reverse if $polyline->first_point->x > $polyline->last_point->x;
    
    my $polyline_bb = $polyline->bounding_box;
    is $polyline->first_point->x, $polyline_bb->x_min, 'expected x_min';
    is $polyline->last_point->x,  $polyline_bb->x_max, 'expected x_max';
    
    is_deeply [ map $_->x, @$polyline ], [ sort map $_->x, @$polyline ],
        'medial axis is not self-overlapping';
}

__END__
