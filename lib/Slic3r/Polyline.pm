package Slic3r::Polyline;
use strict;
use warnings;

use List::Util qw(first);
use Slic3r::Geometry qw(X Y PI epsilon);
use Slic3r::Geometry::Clipper qw(JT_SQUARE);

sub new_scale {
    my $class = shift;
    my @points = map { ref($_) eq 'Slic3r::Point' ? $_->pp : $_ } @_;
    return $class->new(map [ Slic3r::Geometry::scale($_->[X]), Slic3r::Geometry::scale($_->[Y]) ], @points);
}

sub wkt {
    my $self = shift;
    return sprintf "LINESTRING((%s))", join ',', map "$_->[0] $_->[1]", @$self;
}

sub bounding_box {
    my $self = shift;
    return Slic3r::Geometry::BoundingBox->new_from_points([ @$self ]);
}

sub size {
    my $self = shift;
    return [ Slic3r::Geometry::size_2D($self) ];
}

sub is_straight {
    my ($self) = @_;
    
    # Check that each segment's direction is equal to the line connecting
    # first point and last point. (Checking each line against the previous
    # one would have caused the error to accumulate.)
    my $dir = Slic3r::Line->new($self->first_point, $self->last_point)->direction;
    return !defined first { !$_->parallel_to($dir) } @{$self->lines};
}

1;
