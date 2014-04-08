use Test::More tests => 4;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(scale epsilon rad2deg PI);
use Slic3r::Test;

my $flow = Slic3r::Flow->new(width => 0.5, spacing => 0.45, nozzle_diameter => 0.5);
my $bd = Slic3r::Layer::BridgeDetector->new(
    lower_slices    => [],
    perimeter_flow  => $flow,
    infill_flow     => $flow,
);

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
        $bd->lower_slices([$lower]);
        
        ok check_angle($bd->detect_angle($bridge), $expected_angle), 'correct bridge angle for O-shaped overhang';
    };
    
    $test->([20,10], 90);
    $test->([10,20],  0);
}

{
    my $bridge = Slic3r::ExPolygon->new(
        Slic3r::Polygon->new_scale([0,0], [20,0], [20,10], [0,10]),
    );
    my $lower = [
        Slic3r::ExPolygon->new(
            Slic3r::Polygon->new_scale([-2,0], [0,0], [0,10], [-2,10]),
        ),
    ];
    $_->translate(scale 20, scale 20) for $bridge, @$lower; # avoid negative coordinates for easier SVG preview
    
    $lower->[1] = $lower->[0]->clone;
    $lower->[1]->translate(scale 22, 0);
    
    $bd->lower_slices($lower);
    ok check_angle($bd->detect_angle($bridge), 0), 'correct bridge angle for two-sided bridge';
}

{
    my $bridge = Slic3r::ExPolygon->new(
        Slic3r::Polygon->new_scale([0,0], [20,0], [10,10], [0,10]),
    );
    my $lower = [
        Slic3r::ExPolygon->new(
            Slic3r::Polygon->new_scale([0,0], [0,10], [10,10], [10,12], [-2,12], [-2,-2], [22,-2], [22,0]),
        ),
    ];
    $_->translate(scale 20, scale 20) for $bridge, @$lower; # avoid negative coordinates for easier SVG preview
    
    $bd->lower_slices($lower);
    ok check_angle($bd->detect_angle($bridge), 135), 'correct bridge angle for C-shaped overhang';
}

sub check_angle {
    my ($result, $expected) = @_;
    
    # our epsilon is equal to the steps used by the bridge detection algorithm
    ###use XXX; YYY [ rad2deg($result), $expected ];
    return defined $result && abs(rad2deg($result) - $expected) < rad2deg($bd->resolution);
}

__END__
