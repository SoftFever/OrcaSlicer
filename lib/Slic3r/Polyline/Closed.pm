package Slic3r::Polyline::Closed;
use Moo;

extends 'Slic3r::Polyline';

sub lines {
    my $self = shift;
    my @lines = $self->SUPER::lines(@_);
    
    # since this is a closed polyline, we just add a line at the end,
    # connecting the last and the first point
    push @lines, Slic3r::Line->new(points => [$self->points->[-1], $self->points->[0]]);
    return @lines;
}

# superclass doesn't check whether last line of our closed polyline
# is parallel to first one, so let's do it here
sub merge_continuous_lines {
    my $self = shift;
    $self->SUPER::merge_continuous_lines(@_);
    
    my @lines = $self->lines;
    if ($lines[-1]->parallel_to($lines[0])) {
        shift @{$self->points};
    }
}

sub encloses_point {
    my $self = shift;
    my ($point) = @_;
    
    return Slic3r::Geometry::point_in_polygon($point->p, $self->p);
}

1;
