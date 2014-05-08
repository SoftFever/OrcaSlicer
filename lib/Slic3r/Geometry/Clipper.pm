package Slic3r::Geometry::Clipper;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(offset offset_ex
    diff_ex diff union_ex intersection_ex xor_ex JT_ROUND JT_MITER
    JT_SQUARE is_counter_clockwise union_pt offset2 offset2_ex
    intersection intersection_pl diff_pl union CLIPPER_OFFSET_SCALE
    union_pt_chained diff_ppl intersection_ppl);

1;
