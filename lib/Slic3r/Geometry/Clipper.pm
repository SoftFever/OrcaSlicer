package Slic3r::Geometry::Clipper;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(offset offset_ex
    diff_ex diff union_ex intersection_ex xor_ex JT_ROUND JT_MITER
    JT_SQUARE is_counter_clockwise union_pt offset2 offset2_ex traverse_pt
    intersection union CLIPPER_OFFSET_SCALE);

use Slic3r::Geometry qw(scale);

sub traverse_pt {
    my ($polynodes) = @_;
    
    # use a nearest neighbor search to order these children
    # TODO: supply second argument to chained_path_items() too?
    my @nodes = @{Slic3r::Geometry::chained_path_items(
        [ map [ ($_->{outer} ? $_->{outer}[0] : $_->{hole}[0]), $_ ], @$polynodes ],
    )};
    
    my @polygons = ();
    foreach my $polynode (@$polynodes) {
        # traverse the next depth
        push @polygons, traverse_pt($polynode->{children});
        push @polygons, $polynode->{outer} // [ reverse @{$polynode->{hole}} ];
    }
    return @polygons;
}

1;
