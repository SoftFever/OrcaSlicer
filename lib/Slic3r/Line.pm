package Slic3r::Line;
use Moo;
use Scalar::Util qw(weaken);

# arrayref of points
has 'points' => (
    is          => 'rw',
    default     => sub { [] },
    required    => 1,
);

sub cast {
    my $class = shift;
    my ($line) = @_;
    if (ref $line eq 'ARRAY') {
        @$line == 2 or die "Line needs two points!";
        return Slic3r::Line->new(points => [ map Slic3r::Point->cast($_), @$line ]);
    } else {
        return $line;
    }
}

sub a { return $_[0]->points->[0] }
sub b { return $_[0]->points->[1] }

sub id {
    my $self = shift;
    return $self->a->id . "-" . $self->b->id;
}

sub coordinates {
    my $self = shift;
    return ($self->a->coordinates, $self->b->coordinates);
}

sub p {
    my $self = shift;
    return [ $self->a->p, $self->b->p ];
}

sub coincides_with {
    my $self = shift;
    my ($line) = @_;
    
    return ($self->a->coincides_with($line->a) && $self->b->coincides_with($line->b))
        || ($self->a->coincides_with($line->b) && $self->b->coincides_with($line->a));
}

sub has_endpoint {
    my $self = shift;
    my ($point) = @_;
    return $point->coincides_with($self->a) || $point->coincides_with($self->b);
}

sub slope {
    my $self = shift;
    return Slic3r::Geometry::slope($self->p);
}

sub parallel_to {
    my $self = shift;
    my ($line) = @_;
    return Slic3r::Geometry::lines_parallel($self->p, $line->p);
}

1;
