package Slic3r::Fill::Flowsnake;
use Moo;

extends 'Slic3r::Fill::PlanePath';

use Math::PlanePath::Flowsnake;
use Slic3r::Geometry qw(X X1 X2);

# Sorry, this fill is currently broken.

sub process_polyline {
    my $self = shift;
    my ($polyline, $bounding_box) = @_;
    
    $_->[X] += ($bounding_box->[X1] + $bounding_box->[X2]/2) for @$polyline;
}

1;
