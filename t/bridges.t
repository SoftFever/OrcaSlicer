use Test::More tests => 14;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first sum);
use Slic3r;
use Slic3r::Geometry qw(scale epsilon deg2rad rad2deg PI);
use Slic3r::Test;

{
    my $test = sub {
        my ($bridge_size, $rotate, $expected_angle, $tolerance) = @_;
    
        my ($x, $y) = @$bridge_size;
        my $lower = Slic3r::ExPolygon->new(
            Slic3r::Polygon->new_scale([-2,-2], [$x+2,-2], [$x+2,$y+2], [-2,$y+2]),
            Slic3r::Polygon->new_scale([0,0], [0,$y], [$x,$y], [$x,0]),
        );
        $lower->translate(scale 20, scale 20); # avoid negative coordinates for easier SVG preview
        $lower->rotate(deg2rad($rotate), [$x/2,$y/2]);
        my $bridge = $lower->[1]->clone;
        $bridge->reverse;
        $bridge = Slic3r::ExPolygon->new($bridge);
        
        ok check_angle([$lower], $bridge, $expected_angle, $tolerance), 'correct bridge angle for O-shaped overhang';
    };

    $test->([20,10], 0, 90);
    $test->([10,20], 0, 0);
    $test->([20,10], 45, 135, 20);
    $test->([20,10], 135, 45, 20);
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
    
    ok check_angle($lower, $bridge, 0), 'correct bridge angle for two-sided bridge';
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
    
    ok check_angle($lower, $bridge, 135), 'correct bridge angle for C-shaped overhang';
}

{
    my $bridge = Slic3r::ExPolygon->new(
        Slic3r::Polygon->new_scale([10,10],[20,10],[20,20], [10,20]),
    );
    my $lower = [
        Slic3r::ExPolygon->new(
            Slic3r::Polygon->new_scale([10,10],[10,20],[20,20],[20,30],[0,30],[0,10]),
        ),
    ];
    $_->translate(scale 20, scale 20) for $bridge, @$lower; # avoid negative coordinates for easier SVG preview
    
    ok check_angle($lower, $bridge, 45, undef, $bridge->area/2), 'correct bridge angle for square overhang with L-shaped anchors';
}

sub check_angle {
    my ($lower, $bridge, $expected, $tolerance, $expected_coverage) = @_;
    
    if (ref($lower) eq 'ARRAY') {
        $lower = Slic3r::ExPolygon::Collection->new(@$lower);
    }
    
    $expected_coverage //= -1;
    $expected_coverage = $bridge->area if $expected_coverage == -1;
    
    my $bd = Slic3r::BridgeDetector->new($bridge, $lower, scale 0.5);
    
    $tolerance //= rad2deg($bd->resolution) + epsilon;
    $bd->detect_angle;
    my $result = $bd->angle;
    my $coverage = $bd->coverage;
    is sum(map $_->area, @$coverage), $expected_coverage, 'correct coverage area';
    
    # our epsilon is equal to the steps used by the bridge detection algorithm
    ###use XXX; YYY [ rad2deg($result), $expected ];
    # returned value must be non-negative, check for that too
    my $delta=rad2deg($result) - $expected;
    $delta-=180 if $delta>=180 - epsilon;
    return defined $result && $result>=0 && abs($delta) < $tolerance;
}

__END__
