package Slic3r::Geometry::Clipper;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(safety_offset safety_offset_ex offset offset_ex collapse_ex
    diff_ex diff union_ex intersection_ex xor_ex PFT_EVENODD JT_MITER JT_ROUND
    JT_SQUARE is_counter_clockwise union_pt offset2 offset2_ex traverse_pt
    intersection);

use Math::Clipper 1.22 qw(:cliptypes :polyfilltypes :jointypes is_counter_clockwise area);
use Slic3r::Geometry qw(scale);
our $clipper = Math::Clipper->new;

sub safety_offset {
    my ($polygons, $factor) = @_;
    return [ map Slic3r::Polygon->new(@$_),
        @{Math::Clipper::int_offset(_convert($polygons), $factor // (scale 1e-05), 100000, JT_MITER, 2)} ];
}

sub safety_offset_ex {
    my ($polygons, $factor) = @_;
    return map Slic3r::ExPolygon->new($_->{outer}, @{$_->{holes}}),
        @{Math::Clipper::ex_int_offset(_convert($polygons), $factor // (scale 1e-05), 100000, JT_MITER, 2)};
}

sub union_pt {
    my ($polygons, $jointype, $safety_offset) = @_;
    $jointype = PFT_NONZERO unless defined $jointype;
    $clipper->clear;
    $clipper->add_subject_polygons($safety_offset ? _convert(safety_offset($polygons)) : _convert($polygons));
    return $clipper->pt_execute(CT_UNION, $jointype, $jointype);
}

sub collapse_ex {
    my ($polygons, $width) = @_;
    return offset2_ex($polygons, -$width/2, +$width/2);
}

sub simplify_polygon {
    my ($polygon, $pft) = @_;
    return @{ Math::Clipper::simplify_polygon(_convert([$polygon])->[0], $pft // PFT_NONZERO) };
}

sub simplify_polygons {
    my ($polygons, $pft) = @_;
    return @{ Math::Clipper::simplify_polygons(_convert($polygons), $pft // PFT_NONZERO) };
}

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

sub _convert {
    my $p = shift;
    $p = $p->pp if ref($p) ne 'ARRAY' && $p->can('pp');
    return [ map { (ref($_) ne 'ARRAY' && $_->can('pp')) ? $_->pp : $_ } @$p ];
}

1;
