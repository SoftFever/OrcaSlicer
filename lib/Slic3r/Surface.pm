package Slic3r::Surface;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK   = qw(S_TYPE_TOP S_TYPE_BOTTOM S_TYPE_INTERNAL S_TYPE_INTERNALSOLID S_TYPE_INTERNALBRIDGE S_TYPE_INTERNALVOID);
our %EXPORT_TAGS = (types => \@EXPORT_OK);

sub offset {
    my $self = shift;
    return [ map $self->clone(expolygon => $_), @{$self->expolygon->offset_ex(@_)} ];
}

sub p {
    my $self = shift;
    return @{$self->polygons};
}

1;
