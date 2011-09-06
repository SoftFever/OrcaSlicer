package Slic3r::Point;
use Moo;

has 'x' => (
    is          => 'ro',
    #isa         => 'Slic3r::Point::Coordinate',
    required    => 1,
    coerce      => sub { sprintf '%.0f', $_[0] },
);

has 'y' => (
    is          => 'ro',
    #isa         => 'Slic3r::Point::Coordinate',
    required    => 1,
    coerce      => sub { sprintf '%.0f', $_[0] },
);

# this array contains weak references, so it can contain undef's as well
has 'lines' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::Line]',
    default => sub { [] },
);

sub id {
    my $self = shift;
    return $self->x . "," . $self->y; #;;
}

sub coordinates {
    my $self = shift;
    return ($self->x, $self->y); #))
}

sub coincides_with {
    my $self = shift;
    my ($point) = @_;
    
    $point = Slic3r::Point->new(x => $point->[0], y => $point->[1]) #==
        if ref $point eq 'ARRAY';
    return $self->x == $point->x && $self->y == $point->y; #=
}

sub distance_to {
    my $self = shift;
    my ($point) = @_;
    
    return sqrt(($point->x - $self->x)**2 + ($point->y - $self->y)**2); #-
}

1;
