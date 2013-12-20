package Slic3r::Print::State;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK   = qw(STEP_INIT_EXTRUDERS STEP_SLICE STEP_PERIMETERS STEP_PREPARE_INFILL 
                    STEP_INFILL STEP_SUPPORTMATERIAL STEP_SKIRT STEP_BRIM);
our %EXPORT_TAGS = (steps => \@EXPORT_OK);

our %print_steps = map { $_ => 1 } (
    STEP_INIT_EXTRUDERS,
    STEP_SKIRT,
    STEP_BRIM,
);

our %prereqs = (
    STEP_INIT_EXTRUDERS     => [],
    STEP_SLICE              => [],
    STEP_PERIMETERS         => [STEP_SLICE, STEP_INIT_EXTRUDERS],
    STEP_PREPARE_INFILL     => [STEP_PERIMETERS],
    STEP_INFILL             => [STEP_INFILL],
    STEP_SUPPORTMATERIAL    => [STEP_SLICE, STEP_INIT_EXTRUDERS],
    STEP_SKIRT              => [STEP_PERIMETERS, STEP_INFILL],
    STEP_BRIM               => [STEP_PERIMETERS, STEP_INFILL, STEP_SKIRT],
);

1;
