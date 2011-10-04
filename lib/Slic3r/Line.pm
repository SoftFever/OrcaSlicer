package Slic3r::Line;
use Moo;

# arrayref of points
has 'points' => (
    is          => 'rw',
    default     => sub { [] },
    required    => 1,
);

sub cast {
    my $class = shift;
    my ($line, %args) = @_;
    if (ref $line eq 'ARRAY') {
        @$line == 2 or die "Line needs two points!";
        return $class->new(points => [ map Slic3r::Point->cast($_), @$line ], %args);
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

sub has_segment {
    my $self = shift;
    my ($line) = @_;
    
    $line = $line->p if $line->isa('Slic3r::Line');
    
    # a segment belongs to another segment if its points belong to it
    return Slic3r::Geometry::point_in_segment($line->[0], $self->p)
        && Slic3r::Geometry::point_in_segment($line->[1], $self->p);
}

sub parallel_to {
    my $self = shift;
    my ($line) = @_;
    return Slic3r::Geometry::lines_parallel($self->p, $line->p);
}

1;
