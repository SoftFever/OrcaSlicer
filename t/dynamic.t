use Test::More;
use strict;
use warnings;

plan skip_all => 'variable-width paths are currently disabled';
plan tests => 20;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
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
    
    my @offsets = @{$square->noncollapsing_offset_ex(- scale 5)};
    is scalar @offsets, 1, 'non-collapsing offset';
}

{
    local $Slic3r::Config = Slic3r::Config->new(
        perimeters      => 3,
    );
    my $w = 0.7;
    my $perimeter_flow = Slic3r::Flow->new(
        nozzle_diameter => 0.5,
        layer_height    => 0.4,
        width           => $w,
    );
    
    my $print = Slic3r::Print->new;
    my $region = Slic3r::Print::Region->new(
        print => $print,
        flows => { perimeter => $perimeter_flow },
    );
    push @{$print->regions}, $region;
    my $object = Slic3r::Print::Object->new(
        print => $print,
        size  => [1,1],
    );
    my $make_layer = sub {
        my ($width) = @_;
        my $layer = Slic3r::Layer->new(
            object => $object,
            id => 1,
            slices => [
                Slic3r::Surface->new(
                    surface_type    => S_TYPE_INTERNAL,
                    expolygon       => Slic3r::ExPolygon->new([ scale_points [0,0], [50,0], [50,$width], [0,$width] ]),
                ),
            ],
            thin_walls => [],
        );
        my $layerm = $layer->region(0);
        $layer->make_perimeters;
        return $layerm;
    };
    
    my %widths = (
        1   * $w => { perimeters => 1, gaps => 0 },
        1.3 * $w => { perimeters => 1, gaps => 1, gap_flow_spacing => $perimeter_flow->clone(width => 0.2 * $w)->spacing },
        1.5 * $w => { perimeters => 1, gaps => 1, gap_flow_spacing => $perimeter_flow->clone(width => 0.5 * $w)->spacing },
        2   * $w => { perimeters => 1, gaps => 1, gap_flow_spacing => $perimeter_flow->spacing },
        2.5 * $w => { perimeters => 1, gaps => 1, gap_flow_spacing => $perimeter_flow->clone(width => 1.5 * $w)->spacing },
        3   * $w => { perimeters => 2, gaps => 0 },
        4   * $w => { perimeters => 2, gaps => 1, gap_flow_spacing => $perimeter_flow->spacing },
    );
    
    foreach my $width (sort keys %widths) {
        my $layerm = $make_layer->($width);
        is scalar @{$layerm->perimeters}, $widths{$width}{perimeters}, 'right number of perimeters';
        is scalar @{$layerm->thin_fills} ? 1 : 0, $widths{$width}{gaps},
            ($widths{$width}{gaps} ? 'gaps were filled' : 'no gaps detected');  # TODO: we should check the exact number of gaps, but we need a better medial axis algorithm
        
        my @gaps = map $_, @{$layerm->thin_fills};
        if (@gaps) {
            ok +(!first { abs($_->flow_spacing - $widths{$width}{gap_flow_spacing}) > epsilon } @gaps),
                'flow spacing was dynamically adjusted';
        }
    }
}

__END__
