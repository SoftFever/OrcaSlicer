package Slic3r::Line;
use strict;
use warnings;

# a line is a two-points line
use parent 'Slic3r::Polyline';

use Slic3r::Geometry qw(A B X Y);

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

sub grow {
    my $self = shift;
    return Slic3r::Polyline->new(@$self[0,1,0])->grow(@_);
}

1;
