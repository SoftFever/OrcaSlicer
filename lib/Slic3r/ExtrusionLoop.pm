package Slic3r::ExtrusionLoop;
use strict;
use warnings;

use parent qw(Exporter);

our @EXPORT_OK = qw(EXTRL_ROLE_DEFAULT
    EXTRL_ROLE_CONTOUR_INTERNAL_PERIMETER EXTRL_ROLE_SKIRT);
our %EXPORT_TAGS = (roles => \@EXPORT_OK);


1;
