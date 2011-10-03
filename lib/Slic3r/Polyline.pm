package Slic3r::Polyline;
use Moo;

use Math::Clipper qw();
use Sub::Quote;

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
    
    $points = [ map { ref $_ eq 'ARRAY' ? Slic3r::Point->cast($_) : $_ } @$points ];
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
    return [ map $_->p, @{$self->points} ];
}

sub merge_continuous_lines {
    my $self = shift;
    
    my @points = map $_->p, @{$self->points};
    for (my $i = 2; $i <= $#points; $i++) {
        if (Slic3r::Geometry::lines_parallel([$points[$i-2], $points[$i-1]], [$points[$i-1], $points[$i]])) {
            # we can remove $points[$i-1]
            splice @points, $i-1, 1;
            
            $i--;
        }
    }
    
    @{$self->points} = map Slic3r::Point->cast($_), @points;
}

sub cleanup {
    my $self = shift;
    my $tolerance = shift || (1 / $Slic3r::resolution);
    @{$self->points} = map Slic3r::Point->cast($_), 
        Slic3r::Geometry::Douglas_Peucker($self->p, $tolerance);
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
    
    # get point as arrayref
    $point = ref $point eq 'ARRAY' ? $point : $point->p;
    
    $point = Slic3r::Geometry::nearest_point($point, $self->p);
    return Slic3r::Point->cast($point);
}

1;
