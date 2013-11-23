package Slic3r::Geometry::Clipper;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(offset offset_ex
    diff_ex diff union_ex intersection_ex xor_ex JT_ROUND JT_MITER
    JT_SQUARE is_counter_clockwise union_pt offset2 offset2_ex traverse_pt
    intersection intersection_pl diff_pl union CLIPPER_OFFSET_SCALE);

use Slic3r::Geometry qw(scale);
use Slic3r::Geometry qw(chained_path);

sub traverse_pt {
    my ($polynodes) = @_;
    
    # use a nearest neighbor search to order these children
    # TODO: supply second argument to chained_path() too?
    my @ordering_points = map { ($_->{outer} // $_->{hole})->first_point } @$polynodes;
    my @nodes = @$polynodes[ @{chained_path(\@ordering_points)} ];
    
    my @polygons = ();
    foreach my $polynode (@$polynodes) {
        # traverse the next depth
        push @polygons, traverse_pt($polynode->{children});
        push @polygons, $polynode->{outer} // [ reverse @{$polynode->{hole}} ];
    }
    return @polygons;
}

1;
