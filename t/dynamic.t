use Test::More;
use strict;
use warnings;

plan tests => 20;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(X Y scale epsilon);
use Slic3r::Surface ':types';

sub scale_points (@) { map [scale $_->[X], scale $_->[Y]], @_ }

{
    my $square = Slic3r::ExPolygon->new([
        scale_points [0,0], [10,0], [10,10], [0,10],
    ]);
    
    my @offsets = $square->noncollapsing_offset_ex(- scale 5);
    is scalar @offsets, 1, 'non-collapsing offset';
}

{
    my $w = 0.7;
    local $Slic3r::perimeter_flow = Slic3r::Flow->new(
        nozzle_diameter => 0.5,
        layer_height    => 0.4,
        width           => $w,
    );
    local $Slic3r::Config = Slic3r::Config->new(
        perimeters      => 3,
    );
    
    my $make_layer = sub {
        my ($width) = @_;
        my $layer = Slic3r::Layer->new(
            id => 1,
            slices => [
                Slic3r::Surface->new(
                    surface_type    => S_TYPE_INTERNAL,
                    expolygon       => Slic3r::ExPolygon->new([ scale_points [0,0], [50,0], [50,$width], [0,$width] ]),
                ),
            ],
            thin_walls => [],
        );
        $layer->make_perimeters;
        return $layer;
    };
    
    my %widths = (
        1   * $w => { perimeters => 1, gaps => 0 },
        1.3 * $w => { perimeters => 1, gaps => 1, gap_flow_spacing => $Slic3r::perimeter_flow->clone(width => 0.2 * $w)->spacing },
        1.5 * $w => { perimeters => 1, gaps => 1, gap_flow_spacing => $Slic3r::perimeter_flow->clone(width => 0.5 * $w)->spacing },
        2   * $w => { perimeters => 1, gaps => 1, gap_flow_spacing => $Slic3r::perimeter_flow->spacing },
        2.5 * $w => { perimeters => 1, gaps => 1, gap_flow_spacing => $Slic3r::perimeter_flow->clone(width => 1.5 * $w)->spacing },
        3   * $w => { perimeters => 2, gaps => 0 },
        4   * $w => { perimeters => 2, gaps => 1, gap_flow_spacing => $Slic3r::perimeter_flow->spacing },
    );
    
    foreach my $width (sort keys %widths) {
        my $layer = $make_layer->($width);
        is scalar @{$layer->perimeters}, $widths{$width}{perimeters}, 'right number of perimeters';
        is scalar @{$layer->thin_fills} ? 1 : 0, $widths{$width}{gaps},
            ($widths{$width}{gaps} ? 'gaps were filled' : 'no gaps detected');  # TODO: we should check the exact number of gaps, but we need a better medial axis algorithm
        
        my @gaps = map $_->unpack, @{$layer->thin_fills};
        if (@gaps) {
            ok +(!first { abs($_->flow_spacing - $widths{$width}{gap_flow_spacing}) > epsilon } @gaps),
                'flow spacing was dynamically adjusted';
        }
    }
}

__END__
