package Slic3r::Print::State;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK   = qw(STEP_SLICE STEP_PERIMETERS STEP_PREPARE_INFILL 
                    STEP_INFILL STEP_SUPPORTMATERIAL STEP_SKIRT STEP_BRIM);
our %EXPORT_TAGS = (steps => \@EXPORT_OK);

1;
