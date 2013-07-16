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

sub first_point {
    my $self = shift;
    return $self->polygon->[0];
}

sub make_counter_clockwise {
    my $self = shift;
    if (!$self->polygon->is_counter_clockwise) {
        $self->reverse;
        return 1;
    }
    return 0;
}

1;
