package Slic3r::Polyline;
use Moo;

use Math::Clipper qw();
use Slic3r::Geometry qw(polyline_remove_parallel_continuous_edges polyline_remove_acute_vertices
    polygon_remove_acute_vertices polygon_remove_parallel_continuous_edges);
use Sub::Quote;
use XXX;

# arrayref of ordered points
has 'points' => (
    is          => 'rw',
    required    => 1,
    default     => sub { [] },
    isa         => quote_sub q{ use Carp; confess "invalid points" if grep ref $_ ne 'Slic3r::Point', @{$_[0]} },
);

sub id {
    my $self = shift;
    return join ' - ', sort map $_->id, @{$self->points};
}

sub cast {
    my $class = shift;
    my ($points) = @_;
    
    $points = [ map { ref $_ eq 'ARRAY' ? Slic3r::Point->new($_) : $_ } @$points ];
    return $class->new(points => $points);
}

sub lines {
    my $self = shift;
    my @lines = ();
    my $previous_point;
    foreach my $point (@{ $self->points }) {
        if ($previous_point) {
            push @lines, Slic3r::Line->new(points => [ $previous_point, $point ]);
        }
        $previous_point = $point;
    }
    return @lines;
}

sub p {
    my $self = shift;
    return [ @{$self->points} ];
}

sub merge_continuous_lines {
    my $self = shift;
    my $points = $self->p;
    if ($self->isa('Slic3r::Polyline::Closed')) {
        polygon_remove_parallel_continuous_edges($points);
    } else {
        polyline_remove_parallel_continuous_edges($points);
    }
    @{$self->points} = map Slic3r::Point->new($_), @$points;
}

sub remove_acute_vertices {
    my $self = shift;
    my $points = $self->p;
    if ($self->isa('Slic3r::Polyline::Closed')) {
        polygon_remove_acute_vertices($points);
    } else {
        polyline_remove_acute_vertices($points);
    }
    @{$self->points} = map Slic3r::Point->new($_), @$points;
}

sub cleanup {
    my $self = shift;
    my $tolerance = shift || 10;
    
    my $points = $self->p;
    push @$points, $points->[0] if $self->isa('Slic3r::Polyline::Closed');
    my @clean_points = map Slic3r::Point->new($_), 
        Slic3r::Geometry::Douglas_Peucker($self->p, $tolerance);
    pop @clean_points if $self->isa('Slic3r::Polyline::Closed');
    @{$self->points} = @clean_points;
}

sub reverse_points {
    my $self = shift;
    @{$self->points} = reverse @{$self->points};
}

sub is_counter_clockwise {
    my $self = shift;
    return Math::Clipper::is_counter_clockwise($self->p);
}

sub make_counter_clockwise {
    my $self = shift;
    $self->reverse_points if !$self->is_counter_clockwise;
}

sub make_clockwise {
    my $self = shift;
    $self->reverse_points if $self->is_counter_clockwise;
}

sub nearest_point_to {
    my $self = shift;
    my ($point) = @_;
    
    $point = Slic3r::Geometry::nearest_point($point, $self->p);
    return Slic3r::Point->new($point);
}

sub has_segment {
    my $self = shift;
    my ($line) = @_;
    
    for ($self->lines) {
        return 1 if $_->has_segment($line);
    }
    return 0;
}

1;
