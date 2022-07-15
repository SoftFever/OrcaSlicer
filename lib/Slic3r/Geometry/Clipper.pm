package Slic3r::Geometry::Clipper;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(
	offset 
	offset_ex offset2_ex
    diff_ex diff union_ex intersection_ex 
    JT_ROUND JT_MITER JT_SQUARE 
    intersection intersection_pl diff_pl union);

1;
