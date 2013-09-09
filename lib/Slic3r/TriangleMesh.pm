package Slic3r::TriangleMesh;
use Moo;

use List::Util qw(reduce min max first);
use Slic3r::Geometry qw(X Y Z A B unscale same_point);
use Slic3r::Geometry::Clipper qw(union_ex offset);
use Storable;

# public
has 'vertices'      => (is => 'ro', required => 1);         # id => [$x,$y,$z]
has 'facets'        => (is => 'ro', required => 1);         # id => [ $v1_id, $v2_id, $v3_id ]

# private
has 'edges'         => (is => 'rw'); # id => [ $v1_id, $v2_id ]
has 'facets_edges'  => (is => 'rw'); # id => [ $e1_id, $e2_id, $e3_id ]
has 'edges_facets'  => (is => 'rw'); # id => [ $f1_id, $f2_id, (...) ]

use constant MIN => 0;
use constant MAX => 1;

sub analyze {
    my $self = shift;
    
    return if defined $self->edges;
    $self->edges([]);
    $self->facets_edges([]);
    $self->edges_facets([]);
    my %table = ();  # edge_coordinates => edge_id
    my $vertices = $self->vertices;  # save method calls
    
    for (my $facet_id = 0; $facet_id <= $#{$self->facets}; $facet_id++) {
        my $facet = $self->facets->[$facet_id];
        $self->facets_edges->[$facet_id] = [];
        
        # reorder vertices so that the first one is the one with lowest Z
        # this is needed to get all intersection lines in a consistent order
        # (external on the right of the line)
        {
            my $lowest_vertex_idx = reduce {
                $vertices->[ $facet->[$a] ][Z] < $vertices->[ $facet->[$b] ][Z] ? $a : $b
            } -3 .. -1;
            @$facet[-3..-1] = (@$facet[$lowest_vertex_idx..-1], @$facet[-3..($lowest_vertex_idx-1)]);
        }
        
        # ignore the normal if provided
        my @vertices = @$facet[-3..-1];
        
        foreach my $edge ($self->_facet_edges($facet_id)) {
            my $edge_coordinates = join ';', sort @$edge;
            my $edge_id = $table{$edge_coordinates};
            if (!defined $edge_id) {
                # Note that the order of vertices in $self->edges is *casual* because it is only
                # good for one of the two adjacent facets. For this reason, it must not be used
                # when dealing with single facets.
                push @{$self->edges}, $edge;
                $edge_id = $#{$self->edges};
                $table{$edge_coordinates} = $edge_id;
                $self->edges_facets->[$edge_id] = [];
            }
            
            push @{$self->facets_edges->[$facet_id]}, $edge_id;
            push @{$self->edges_facets->[$edge_id]}, $facet_id;
        }
    }
}

sub merge {
    my $class = shift;
    my @meshes = @_;
    
    my $vertices = [];
    my $facets = [];
    
    foreach my $mesh (@meshes) {
        my $v_offset = @$vertices;
        push @$vertices, @{$mesh->vertices};
        push @$facets, map {
            my $f = [@$_];
            $f->[$_] += $v_offset for -3..-1;
            $f;
        } @{$mesh->facets};
    }
    
    return $class->new(vertices => $vertices, facets => $facets);
}

sub clone {
  Storable::dclone($_[0])
}

sub check_manifoldness {
    my $self = shift;
    
    $self->analyze;
    
    # look for any edges belonging to an odd number of facets
    # we should actually check that each pair of facets belonging to this edge
    # has compatible winding order
    my ($first_bad_edge_id) =
        grep { @{ $self->edges_facets->[$_] } % 2 } 0..$#{$self->edges_facets};
    if (defined $first_bad_edge_id) {
        warn sprintf "Warning: The input file contains a hole near edge %f,%f,%f-%f,%f,%f (not manifold). "
            . "You might want to repair it and retry, or to check the resulting G-code before printing anyway.\n",
            map @{$self->vertices->[$_]}, @{$self->edges->[$first_bad_edge_id]};
        return 0;
    }
    
    # empty the edges array as we don't really need it anymore
    @{$self->edges} = ();
    
    return 1;
}

sub rotate {
    my $self = shift;
    my ($deg, $center) = @_;
    return if $deg == 0;
    
    my $rad = Slic3r::Geometry::deg2rad($deg);
    
    # transform vertex coordinates
    foreach my $vertex (@{$self->vertices}) {
        @$vertex = (@{ +(Slic3r::Geometry::rotate_points($rad, $center, [ $vertex->[X], $vertex->[Y] ]))[0] }, $vertex->[Z]);
    }
}

sub scale {
    my $self = shift;
    my ($factor) = @_;
    return if $factor == 1;
    
    # transform vertex coordinates
    foreach my $vertex (@{$self->vertices}) {
        $vertex->[$_] *= $factor for X,Y,Z;
    }
}

sub scale_xyz {
    my $self = shift;
    my ($versor) = @_;
    
    # transform vertex coordinates
    foreach my $vertex (@{$self->vertices}) {
        $vertex->[$_] *= $versor->[$_] for X,Y,Z;
    }
}

sub move {
    my $self = shift;
    my (@shift) = @_;
    
    # transform vertex coordinates
    foreach my $vertex (@{$self->vertices}) {
        $vertex->[$_] += $shift[$_] || 0 for X,Y,Z;
    }
}

sub align_to_origin {
    my $self = shift;
    
    # calculate the displacements needed to 
    # have lowest value for each axis at coordinate 0
    my $bb = $self->bounding_box;
    $self->move(map -$bb->extents->[$_][MIN], X,Y,Z);
}

sub center_around_origin {
    my $self = shift;
    
    $self->move(map -$_, @{ $self->center });
}

sub center {
    my $self = shift;
    return $self->bounding_box->center;
}

sub duplicate {
    my $self = shift;
    my (@shifts) = @_;
    
    my @new_facets = ();
    foreach my $facet (@{$self->facets}) {
        # transform vertex coordinates
        my ($normal, @vertices) = @$facet;
        foreach my $shift (@shifts) {
            push @new_facets, [ $normal ];
            foreach my $vertex (@vertices) {
                push @{$self->vertices}, [ map $self->vertices->[$vertex][$_] + ($shift->[$_] || 0), (X,Y,Z) ];
                push @{$new_facets[-1]}, $#{$self->vertices};
            }
        }
    }
    push @{$self->facets}, @new_facets;
    $self->BUILD;
}

sub used_vertices {
    my $self = shift;
    return [ map $self->vertices->[$_], map @$_, @{$self->facets} ];
}

sub bounding_box {
    my $self = shift;
    return Slic3r::Geometry::BoundingBox->new_from_points_3D($self->used_vertices);
}

sub size {
    my $self = shift;
    return $self->bounding_box->size;
}

# this will return *scaled* expolygons, so it is expected to be run
# on unscaled meshes
sub horizontal_projection {
    my $self = shift;
    
    my @f = ();
    foreach my $facet (@{$self->facets}) {
        push @f, Slic3r::Polygon->new(
            map [ map $_ / &Slic3r::SCALING_FACTOR, @{$self->vertices->[$_]}[X,Y] ], @$facet
        );
    }
    
    $_->make_counter_clockwise for @f;  # do this after scaling, as winding order might change while doing that
    
    # the offset factor was tuned using groovemount.stl
    return union_ex(offset(\@f, Slic3r::Geometry::scale 0.01), 1);
}

1;
