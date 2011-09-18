package Slic3r::Point;
use Moo;

has 'x' => (
    is          => 'ro',
    required    => 1,
    coerce      => sub { sprintf '%.0f', $_[0] },
);

has 'y' => (
    is          => 'ro',
    required    => 1,
    coerce      => sub { sprintf '%.0f', $_[0] },
);

sub cast {
    my $class = shift;
    my ($point) = @_;
    return ref $point eq 'ARRAY' 
        ? Slic3r::Point->new(x => $point->[0], y => $point->[1])  # ==
        : $point;
}

sub id {
    my $self = shift;
    return $self->x . "," . $self->y; #;;
}

sub coordinates {
    my $self = shift;
    return ($self->x, $self->y); #))
}

sub p {
    my $self = shift;
    return [ $self->coordinates ];
}

sub coincides_with {
    my $self = shift;
    my ($point) = @_;
    return Slic3r::Geometry::points_coincide($self->p, $point->p);
}

sub distance_to {
    my $self = shift;
    my ($point) = @_;
    return Slic3r::Geometry::distance_between_points($self->p, $point->p);
}

1;
