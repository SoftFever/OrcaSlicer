package Slic3r::Polyline;
use Moo;

use Math::Clipper qw();
use Slic3r::Geometry qw(A B polyline_remove_parallel_continuous_edges polyline_remove_acute_vertices
    polygon_remove_acute_vertices polygon_remove_parallel_continuous_edges move_points);
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
    my ($points, %args) = @_;
    
    $points = [ map Slic3r::Point->cast($_), @$points ];
    return $class->new(points => $points, %args);
}

sub lines {
    my $self = shift;
    my @lines = ();
    my $previous_point;
    foreach my $point (@{ $self->points }) {
        if ($previous_point) {
            push @lines, Slic3r::Line->new($previous_point, $point);
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

sub clip_with_polygon {
    my $self = shift;
    my ($polygon) = @_;
    
    return $self->clip_with_expolygon(Slic3r::ExPolygon->new($polygon));
}

sub clip_with_expolygon {
    my $self = shift;
    my ($expolygon) = @_;
    
    my @polylines = ();
    my $current_polyline = [];
    foreach my $line ($self->lines) {
        my ($first_line, @other_lines) = @{ $expolygon->clip_line($line) };
        next unless $first_line;
        
        if (!@$current_polyline) {
            push @$current_polyline, @$first_line;
        } elsif ($first_line->[A]->coincides_with($current_polyline->[-1])) {
            push @$current_polyline, $first_line->[B];
        } else {
            push @polylines, $current_polyline;
            $current_polyline = [ @$first_line ];
        }
        
        foreach my $other_line (@other_lines) {
            if (@$current_polyline) {
                push @polylines, $current_polyline;
                $current_polyline = [];
            }
            push @polylines, [ @$other_line ];
        }
    }
    if (@$current_polyline) {
        push @polylines, $current_polyline;
    }
    
    if (@polylines > 1 && scalar(@{$polylines[-1]}) == 2 && $polylines[-1][-1] eq $polylines[0][0]) {
        unshift @{$polylines[0]}, $polylines[-1][0];
        pop @polylines;
    }
    
    return map Slic3r::Polyline->cast($_), @polylines;
}

sub bounding_box {
    my $self = shift;
    return Slic3r::Geometry::bounding_box($self->points);
}

sub translate {
    my $self = shift;
    my ($x, $y) = @_;
    @{$self->points} = move_points([$x, $y], @{$self->points});
}

1;
