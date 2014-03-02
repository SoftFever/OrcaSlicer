package Slic3r::TriangleMesh;
use strict;
use warnings;

use List::Util qw(first);
use Slic3r::Geometry qw(X Y);
use Slic3r::Geometry::Clipper qw(union_ex offset);

sub needed_repair {
    my $self = shift;
    
    my $stats = $self->stats;
    return (first { $stats->{$_} > 0 }
        qw(degenerate_facets edges_fixed facets_removed facets_added facets_reversed backwards_edges)) ? 1 : 0;
}

sub center {
    my $self = shift;
    return $self->bounding_box->center;
}

1;
