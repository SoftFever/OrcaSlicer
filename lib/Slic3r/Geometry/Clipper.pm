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
    return Math::Clipper::int_offset($polygons, $factor // (scale 1e-05), 100000, JT_MITER, 2);
}

sub safety_offset_ex {
    my ($polygons, $factor) = @_;
    return map Slic3r::ExPolygon->new($_),
        @{Math::Clipper::ex_int_offset($polygons, $factor // (scale 1e-05), 100000, JT_MITER, 2)};
}

sub offset {
    my ($polygons, $distance, $scale, $joinType, $miterLimit) = @_;
    $scale      ||= 100000;
    $joinType   //= JT_MITER;
    $miterLimit //= 3;
    
    my $offsets = Math::Clipper::int_offset($polygons, $distance, $scale, $joinType, $miterLimit);
    return @$offsets;
}

sub offset2 {
    my ($polygons, $distance1, $distance2, $scale, $joinType, $miterLimit) = @_;
    $scale      ||= 100000;
    $joinType   //= JT_MITER;
    $miterLimit //= 3;
    
    my $offsets = Math::Clipper::int_offset2($polygons, $distance1, $distance2, $scale, $joinType, $miterLimit);
    return @$offsets;
}

sub offset_ex {
    my ($polygons, $distance, $scale, $joinType, $miterLimit) = @_;
    $scale      ||= 100000;
    $joinType   //= JT_MITER;
    $miterLimit //= 3;
    
    my $offsets = Math::Clipper::ex_int_offset($polygons, $distance, $scale, $joinType, $miterLimit);
    return map Slic3r::ExPolygon->new($_), @$offsets;
}

sub offset2_ex {
    my ($polygons, $delta1, $delta2, $scale, $joinType, $miterLimit) = @_;
    $scale      ||= 100000;
    $joinType   //= JT_MITER;
    $miterLimit //= 3;
    
    my $offsets = Math::Clipper::ex_int_offset2($polygons, $delta1, $delta2, $scale, $joinType, $miterLimit);
    return map Slic3r::ExPolygon->new($_), @$offsets;
}

sub diff_ex {
    my ($subject, $clip, $safety_offset) = @_;
    
    $clipper->clear;
    $clipper->add_subject_polygons($subject);
    $clipper->add_clip_polygons($safety_offset ? safety_offset($clip) : $clip);
    return [
        map Slic3r::ExPolygon->new($_),
            @{ $clipper->ex_execute(CT_DIFFERENCE, PFT_NONZERO, PFT_NONZERO) },
    ];
}

sub diff {
    my ($subject, $clip, $safety_offset) = @_;
    
    $clipper->clear;
    $clipper->add_subject_polygons($subject);
    $clipper->add_clip_polygons($safety_offset ? safety_offset($clip) : $clip);
    return [
        map Slic3r::Polygon->new($_),
            @{ $clipper->execute(CT_DIFFERENCE, PFT_NONZERO, PFT_NONZERO) },
    ];
}

sub union_ex {
    my ($polygons, $jointype, $safety_offset) = @_;
    $jointype = PFT_NONZERO unless defined $jointype;
    $clipper->clear;
    $clipper->add_subject_polygons($safety_offset ? safety_offset($polygons) : $polygons);
    return [
        map Slic3r::ExPolygon->new($_),
            @{ $clipper->ex_execute(CT_UNION, $jointype, $jointype) },
    ];
}

sub union_pt {
    my ($polygons, $jointype, $safety_offset) = @_;
    $jointype = PFT_NONZERO unless defined $jointype;
    $clipper->clear;
    $clipper->add_subject_polygons($safety_offset ? safety_offset($polygons) : $polygons);
    return $clipper->pt_execute(CT_UNION, $jointype, $jointype);
}

sub intersection_ex {
    my ($subject, $clip, $jointype, $safety_offset) = @_;
    $jointype = PFT_NONZERO unless defined $jointype;
    $clipper->clear;
    $clipper->add_subject_polygons($subject);
    $clipper->add_clip_polygons($safety_offset ? safety_offset($clip) : $clip);
    return [
        map Slic3r::ExPolygon->new($_),
            @{ $clipper->ex_execute(CT_INTERSECTION, $jointype, $jointype) },
    ];
}

sub intersection {
    my ($subject, $clip, $jointype, $safety_offset) = @_;
    $jointype = PFT_NONZERO unless defined $jointype;
    $clipper->clear;
    $clipper->add_subject_polygons($subject);
    $clipper->add_clip_polygons($safety_offset ? safety_offset($clip) : $clip);
    return [
        map Slic3r::Polygon->new($_),
            @{ $clipper->execute(CT_INTERSECTION, $jointype, $jointype) },
    ];
}

sub xor_ex {
    my ($subject, $clip, $jointype) = @_;
    $jointype = PFT_NONZERO unless defined $jointype;
    $clipper->clear;
    $clipper->add_subject_polygons($subject);
    $clipper->add_clip_polygons($clip);
    return [
        map Slic3r::ExPolygon->new($_),
            @{ $clipper->ex_execute(CT_XOR, $jointype, $jointype) },
    ];
}

sub collapse_ex {
    my ($polygons, $width) = @_;
    return [ offset2_ex($polygons, -$width/2, +$width/2) ];
}

sub simplify_polygon {
    my ($polygon, $pft) = @_;
    return @{ Math::Clipper::simplify_polygon($polygon, $pft // PFT_NONZERO) };
}

sub simplify_polygons {
    my ($polygons, $pft) = @_;
    return @{ Math::Clipper::simplify_polygons($polygons, $pft // PFT_NONZERO) };
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

1;
