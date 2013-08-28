package Slic3r::ExtrusionLoop;
use strict;
use warnings;

sub split_at {
    my $self = shift;
    
    return Slic3r::ExtrusionPath->new(
        polyline        => $self->polygon->split_at(@_),
        role            => $self->role,
        flow_spacing    => $self->flow_spacing,
        height          => $self->height,
    );
}

1;
