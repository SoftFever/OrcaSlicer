package Slic3r::Polyline::Closed;
use Moo;

extends 'Slic3r::Polyline';

use Math::Clipper qw(JT_MITER);

sub lines {
    my $self = shift;
    my @lines = $self->SUPER::lines(@_);
    
    # since this is a closed polyline, we just add a line at the end,
    # connecting the last and the first point
    push @lines, Slic3r::Line->new(points => [$self->points->[-1], $self->points->[0]]);
    return @lines;
}

sub encloses_point {
    my $self = shift;
    my ($point) = @_;
    
    return Slic3r::Geometry::point_in_polygon($point->p, $self->p);
}

# returns false if the polyline is too tight to be printed
sub is_printable {
    my $self = shift;
    
    # try to get an inwards offset
    # for a distance equal to half of the extrusion width;
    # if no offset is possible, then polyline is not printable
    my $p = $self->p;
    @$p = reverse @$p if !Math::Clipper::is_counter_clockwise($p);
    my $offsets = Math::Clipper::offset([$p], -($Slic3r::flow_width / 2 / $Slic3r::resolution), $Slic3r::resolution * 100000, JT_MITER, 2);
    return @$offsets ? 1 : 0;
}

1;
