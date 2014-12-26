package Slic3r::Polyline;
use strict;
use warnings;

use Slic3r::Geometry qw(X Y);

sub new_scale {
    my $class = shift;
    my @points = map { ref($_) eq 'Slic3r::Point' ? $_->pp : $_ } @_;
    return $class->new(map [ Slic3r::Geometry::scale($_->[X]), Slic3r::Geometry::scale($_->[Y]) ], @points);
}

sub dump_perl {
    my $self = shift;
    return sprintf "[%s]", join ',', map "[$_->[0],$_->[1]]", @$self;
}

1;
