package Slic3r::Line::FacetEdge;
use Moo;

extends 'Slic3r::Line';

has 'edge_type' => (is => 'ro');  # top/bottom

1;
