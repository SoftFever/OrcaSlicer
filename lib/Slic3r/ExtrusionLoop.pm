package Slic3r::ExtrusionLoop;
use strict;
use warnings;

sub split_at {
    my $self = shift;
    
    return Slic3r::ExtrusionPath->new(
        polyline        => $self->polygon->split_at(@_),
        role            => $self->role,
        mm3_per_mm      => $self->mm3_per_mm,
        width           => $self->width,
        height          => $self->height,
    );
}

1;
