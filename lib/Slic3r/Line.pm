package Slic3r::Line;
use strict;
use warnings;

use Boost::Geometry::Utils;
use Slic3r::Geometry qw(A B X Y);

sub new {
    my $class = shift;
    my $self;
    $self = [ @_ ];
    bless $self, $class;
    bless $_, 'Slic3r::Point' for @$self;
    return $self;
}

sub boost_linestring {
    my $self = shift;
    return Boost::Geometry::Utils::linestring($self);
}

sub coincides_with {
    my $self = shift;
    my ($line) = @_;
    
    return ($self->a->coincides_with($line->a) && $self->b->coincides_with($line->b))
        || ($self->a->coincides_with($line->b) && $self->b->coincides_with($line->a));
}

sub length {
    my $self = shift;
    return Slic3r::Geometry::line_length($self);
}

sub vector {
    my $self = shift;
    return (ref $self)->new([0,0], [map $self->[B][$_] - $self->[A][$_], X,Y]);
}

sub atan {
    my $self = shift;
    return Slic3r::Geometry::line_atan($self);
}

sub direction {
    my $self = shift;
    return Slic3r::Geometry::line_direction($self);
}

sub intersection {
    my $self = shift;
    my ($line, $require_crossing) = @_;
    return Slic3r::Geometry::line_intersection($self, $line, $require_crossing);
}

sub point_on_left {
    my $self = shift;
    my ($point) = @_;
    return Slic3r::Geometry::point_is_on_left_of_segment($point, $self);
}

sub midpoint {
    my $self = shift;
    return Slic3r::Point->new(
        ($self->[A][X] + $self->[B][X]) / 2,
        ($self->[A][Y] + $self->[B][Y]) / 2,
    );
}

sub reverse {
    my $self = shift;
    @$self = reverse @$self;
}

sub translate {
    my $self = shift;
    my ($x, $y) = @_;
    @$self = Slic3r::Geometry::move_points([$x, $y], @$self);
    bless $_, 'Slic3r::Point' for @$self;
    return $self;
}

1;
