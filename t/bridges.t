use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(scale);
use Slic3r::Test;

my $flow = Slic3r::Flow->new(width => 0.5, spacing => 0.45, nozzle_diameter => 0.5);

{
    my $lower = Slic3r::ExPolygon->new(
        Slic3r::Polygon->new_scale([0,0], [20,0], [20,10], [0,10]),
        Slic3r::Polygon->new_scale([2,2], [2,8], [18,8], [18,2]),
    );
    my $bridge = $lower->[1]->clone;
    $bridge->reverse;
    $bridge = Slic3r::ExPolygon->new($bridge);
    my $bd = Slic3r::Layer::BridgeDetector->new(
        lower_slices    => [$lower],
        perimeter_flow  => $flow,
        infill_flow     => $flow,
    );
    # 0 is North/South
    is $bd->detect_angle($bridge), 0, 'correct bridge angle detected';
}

__END__
