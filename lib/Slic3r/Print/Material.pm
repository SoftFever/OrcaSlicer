package Slic3r::Print::Material;
use Moo;

has 'extruders'         => (is => 'rw', default => sub { {} }); # by role
has 'flows'             => (is => 'rw', default => sub { {} }); # by role
has 'first_layer_flows' => (is => 'rw', default => sub { {} }); # by role

1;
