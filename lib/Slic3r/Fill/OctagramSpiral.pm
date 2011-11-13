package Slic3r::Fill::OctagramSpiral;
use Moo;

extends 'Slic3r::Fill::PlanePath';
use Math::PlanePath::OctagramSpiral;

sub multiplier () { sqrt(2) }

1;
