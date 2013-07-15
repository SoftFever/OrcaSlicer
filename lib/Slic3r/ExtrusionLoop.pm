package Slic3r::ExtrusionLoop;
use strict;
use warnings;

use Slic3r::Geometry qw(same_point);

sub polygon { $_[0] }

# class or object method
sub pack {
    my $self = shift;
    
    if (ref $self) {
        return $self;
    } else {
        return $self->new(@_);
    }
}

# no-op
sub unpack { $_[0] }

sub split_at_index {
    my $self = shift;
    
    return Slic3r::ExtrusionPath->new(
        polyline        => $self->as_polygon->split_at_index(@_),
        role            => $self->role,
        flow_spacing    => $self->flow_spacing,
        height          => $self->height,
    );
}

sub split_at {
    my $self = shift;
    
    return Slic3r::ExtrusionPath->new(
        polyline        => $self->as_polygon->split_at(@_),
        role            => $self->role,
        flow_spacing    => $self->flow_spacing,
        height          => $self->height,
    );
}

sub split_at_first_point {
    my $self = shift;
    return $self->split_at_index(0);
}

sub first_point {
    my $self = shift;
    return $self->polygon->[0];
}

1;
