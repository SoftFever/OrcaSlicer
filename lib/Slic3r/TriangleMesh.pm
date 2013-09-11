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

sub facets_count {
    my $self = shift;
    return $self->stats->{number_of_facets};
}

sub bounding_box {
    my $self = shift;
    return Slic3r::Geometry::BoundingBox->new_from_bb($self->bb3);
}

# this will return *scaled* expolygons, so it is expected to be run
# on unscaled meshes
sub horizontal_projection {
    my $self = shift;
    
    my ($facets, $vertices) = ($self->facets, $self->vertices);
    
    my @f = ();
    foreach my $facet (@$facets) {
        push @f, Slic3r::Polygon->new(
            map [ map $_ / &Slic3r::SCALING_FACTOR, @{$vertices->[$_]}[X,Y] ], @$facet
        );
    }
    
    $_->make_counter_clockwise for @f;  # do this after scaling, as winding order might change while doing that
    
    # the offset factor was tuned using groovemount.stl
    return union_ex(offset(\@f, Slic3r::Geometry::scale 0.01), 1);
}

1;
