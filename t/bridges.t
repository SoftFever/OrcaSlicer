use Test::More tests => 2;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(scale epsilon rad2deg);
use Slic3r::Test;

my $flow = Slic3r::Flow->new(width => 0.5, spacing => 0.45, nozzle_diameter => 0.5);

{
    my $test = sub {
        my ($bridge_size, $expected_angle) = @_;
        
        my ($x, $y) = @$bridge_size;
        my $lower = Slic3r::ExPolygon->new(
            Slic3r::Polygon->new_scale([-2,-2], [$x+2,-2], [$x+2,$y+2], [-2,$y+2]),
            Slic3r::Polygon->new_scale([0,0], [0,$y], [$x,$y], [$x,0]),
        );
        $lower->translate(scale 20, scale 20); # avoid negative coordinates for easier SVG preview
        my $bridge = $lower->[1]->clone;
        $bridge->reverse;
        $bridge = Slic3r::ExPolygon->new($bridge);
        my $bd = Slic3r::Layer::BridgeDetector->new(
            lower_slices    => [$lower],
            perimeter_flow  => $flow,
            infill_flow     => $flow,
        );
        
        ok abs(rad2deg($bd->detect_angle($bridge)) - $expected_angle) < epsilon, 'correct bridge angle detected';
    };
    
    $test->([20,10], 90);
    $test->([10,20],  0);
}

__END__
