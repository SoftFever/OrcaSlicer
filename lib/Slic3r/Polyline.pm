package Slic3r::Polyline;
use strict;
use warnings;

use Slic3r::Geometry qw(A B X Y X1 X2 Y1 Y2);
use Slic3r::Geometry::Clipper qw(JT_SQUARE);
use Storable qw();

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

1;
