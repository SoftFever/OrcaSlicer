package Slic3r::TriangleMesh;
use Moo;

use Slic3r::Geometry qw(X Y Z A B epsilon same_point);
use XXX;

# public
has 'vertices'      => (is => 'ro', required => 1);         # id => [ [$x1,$y1],[$x2,$y2],[$x3,$y3] ]
has 'facets'        => (is => 'ro', required => 1);         # id => [ $normal, $v1_id, $v2_id, $v3_id ]

# private
has 'edges'         => (is => 'ro', default => sub { [] }); # id => [ $v1_id, $v2_id ]
has 'facets_edges'  => (is => 'ro', default => sub { [] }); # id => [ $e1_id, $e2_id, $e3_id ]
has 'edges_facets'  => (is => 'ro', default => sub { [] }); # id => [ $f1_id, $f2_id, (...) ]

use constant MIN => 0;
use constant MAX => 1;

# always make sure BUILD is idempotent
sub BUILD {
    my $self = shift;
    
    @{$self->edges} = ();
    @{$self->facets_edges} = ();
    @{$self->edges_facets} = ();
    my %table = ();  # edge_coordinates => edge_id
    
    for (my $facet_id = 0; $facet_id <= $#{$self->facets}; $facet_id++) {
        my $facet = $self->facets->[$facet_id];
        $self->facets_edges->[$facet_id] = [];
        
        # reorder vertices so that the first one is the one with lowest Z
        # this is needed to get all intersection lines in a consistent order
        # (external on the right of the line)
        {
            my @z_order = sort { $self->vertices->[$facet->[$a]][Z] <=> $self->vertices->[$facet->[$b]][Z] } 1..3;
            @$facet[1..3] = (@$facet[$z_order[0]..3], @$facet[1..($z_order[0]-1)]);
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

sub _facet_edges {
    my $self = shift;
    my ($facet_id) = @_;
    
    my $facet = $self->facets->[$facet_id];
    return (
        [ $facet->[1], $facet->[2] ],
        [ $facet->[2], $facet->[3] ],
        [ $facet->[3], $facet->[1] ],
    );
}

# This method is supposed to remove narrow triangles, but it actually doesn't 
# work much; I'm committing it for future reference but I'm going to remove it later.
# Note: a 'clean' method should actually take care of non-manifold facets and remove
# them.
sub clean {
    my $self = shift;
    
    # retrieve all edges shared by more than two facets;
    my @weird_edges = grep { @{$self->edge_facets->{$_}} != 2 } keys %{$self->edge_facets};
    
    # usually most of these facets are very narrow triangles whose two edges
    # are detected as collapsed, and thus added twice to the edge in edge_fasets table
    # let's identify these triangles
    my @narrow_facets_indexes = ();
    foreach my $edge_id (@weird_edges) {
        my %facet_count = ();
        $facet_count{$_}++ for @{$self->edge_facets->{$edge_id}};
        @{$self->edge_facets->{$edge_id}} = grep $facet_count{$_} == 1, keys %facet_count;
        push @narrow_facets_indexes, grep $facet_count{$_} > 1, keys %facet_count;
    }
    
    # remove identified narrow facets
    foreach my $facet_id (@narrow_facets_indexes) {last;
         splice @{$self->facets}, $facet_id, 1;
         splice @{$self->facets_edges}, $facet_id, 1;
         foreach my $facet_ides (values %{$self->edge_facets}) {
            @$facet_ides = map { $_ > $facet_id ? ($_-1) : $_ } @$facet_ides;
         }
    }
    
    Slic3r::debugf "%d narrow facets removed\n", scalar(@narrow_facets_indexes)
        if @narrow_facets_indexes;
}

sub check_manifoldness {
    my $self = shift;
    
    if (grep { @$_ != 2 } @{$self->edges_facets}) {
        warn "Warning: The input file is not manifold. You might want to check the "
            . "resulting gcode before printing.\n";
    }
}

sub make_loops {
    my $self = shift;
    my ($layer) = @_;
    
    my @lines = @{$layer->lines};
    
    # remove tangent edges
    {
        for (my $i = 0; $i <= $#lines; $i++) {
            next unless defined $lines[$i] && $lines[$i]->facet_edge;
            # if the line is a facet edge, find another facet edge
            # having the same endpoints but in reverse order
            for (my $j = $i+1; $j <= $#lines; $j++) {
                next unless defined $lines[$j] && $lines[$j]->facet_edge;
                
                # are these facets adjacent? (sharing a common edge on this layer)
                if ($lines[$i]->a_id == $lines[$j]->b_id && $lines[$i]->b_id == $lines[$j]->a_id) {
                
                    # if they are both oriented upwards or downwards (like a 'V')
                    # then we can remove both edges from this layer since it won't 
                    # affect the sliced shape
                    if ($lines[$j]->facet_edge eq $lines[$i]->facet_edge) {
                        $lines[$i] = undef;
                        $lines[$j] = undef;
                        last;
                    }
                    
                    # if one of them is oriented upwards and the other is oriented
                    # downwards, let's only keep one of them (it doesn't matter which
                    # one since all 'top' lines were reversed at slicing)
                    if ($lines[$i]->facet_edge eq 'top' && $lines[$j]->facet_edge eq 'bottom') {
                        $lines[$j] = undef;
                        last;
                    }
                }
                
            }
        }
    }
    
    @lines = grep $_, @lines;
    
    # count relationships
    my %prev_count = ();  # how many lines have the same prev_facet_index
    my %a_count    = ();  # how many lines have the same a_id
    foreach my $line (@lines) {
        if (defined $line->prev_facet_index) {
            $prev_count{$line->prev_facet_index}++;
        }
        if (defined $line->a_id) {
            $a_count{$line->a_id}++;
        }
    }
    
    foreach my $point_id (grep $a_count{$_} > 1, keys %a_count) {
        my @lines_starting_here = grep defined $_->a_id && $_->a_id == $point_id, @lines;
        Slic3r::debugf "%d lines start at point %d\n", scalar(@lines_starting_here), $point_id;
        
        # if two lines start at this point, one being a 'top' facet edge and the other being a 'bottom' one,
        # then remove the top one and those following it (removing the top or the bottom one is an arbitrary
        # choice)
        if (@lines_starting_here == 2 && join(',', sort map $_->facet_edge, @lines_starting_here) eq 'bottom,top') {
            my @to_remove = grep $_->facet_edge eq 'top', @lines_starting_here;
            while (!grep defined $_->b_id && $_->b_id == $to_remove[-1]->b_id && $_ ne $to_remove[-1], @lines) {
                push @to_remove, grep defined $_->a_id && $_->a_id == $to_remove[-1]->b_id, @lines;
            }
            my %to_remove = map {$_ => 1} @to_remove;
            @lines = grep !$to_remove{$_}, @lines;
        } else {
            Slic3r::debugf "  this shouldn't happen and should be further investigated\n";
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output(undef, "same_point.svg",
                    lines       => [ map $_->line, grep !$_->facet_edge, @lines ],
                    red_lines   => [ map $_->line, grep $_->facet_edge, @lines ],
                    points      => [ $self->vertices->[$point_id] ],
                    no_arrows => 0,
                );
            }
        }
    }
    
    # optimization: build indexes of lines
    my %by_facet_index = map { $lines[$_]->facet_index => $_ }
        grep defined $lines[$_]->facet_index,
        (0..$#lines);
    my %by_a_id = map { $lines[$_]->a_id => $_ }
        grep defined $lines[$_]->a_id,
        (0..$#lines);
    
    my (@polygons, %visited_lines) = ();
    CYCLE: for (my $i = 0; $i <= $#lines; $i++) {
        my $line = $lines[$i];
        next if $visited_lines{$line};
        my @points = ();
        my $first_facet_index = $line->facet_index;
        
        do {
            my $next_line;
            if (defined $line->next_facet_index && exists $by_facet_index{$line->next_facet_index}) {
                $next_line = $lines[$by_facet_index{$line->next_facet_index}];
            } elsif (defined $line->b_id && exists $by_a_id{$line->b_id}) {
                $next_line = $lines[$by_a_id{$line->b_id}];
            } else {
                Slic3r::debugf "  line has no next_facet_index or b_id\n";
                $layer->slicing_errors(1);
                next CYCLE;
            }
            
            if (!$next_line || $visited_lines{$next_line}) {
                Slic3r::debugf "  failed to close this loop\n";
                $layer->slicing_errors(1);
                next CYCLE;
            } elsif (defined $next_line->prev_facet_index && $next_line->prev_facet_index != $line->facet_index) {
                Slic3r::debugf "  wrong prev_facet_index\n";
                $layer->slicing_errors(1);
                next CYCLE;
            } elsif (defined $next_line->a_id && $next_line->a_id != $line->b_id) {
                Slic3r::debugf "  wrong a_id\n";
                $layer->slicing_errors(1);
                next CYCLE;
            }
            
            push @points, $next_line->b;
            $visited_lines{$next_line} = 1;
            $line = $next_line;
        } while ($first_facet_index != $line->facet_index);
    
        Slic3r::debugf "  Discovered polygon of %d points\n", scalar(@points);
        push @polygons, Slic3r::Polygon->new(@points);
        pop @polygons if !$polygons[-1]->cleanup;
    }
    
    return [@polygons];
}

sub rotate {
    my $self = shift;
    my ($deg) = @_;
    return if $deg == 0;
    
    my $rad = Slic3r::Geometry::deg2rad($deg);
    
    # transform vertex coordinates
    foreach my $vertex (@{$self->vertices}) {
        @$vertex = (@{ +(Slic3r::Geometry::rotate_points($rad, undef, [ $vertex->[X], $vertex->[Y] ]))[0] }, $vertex->[Z]);
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

sub move {
    my $self = shift;
    my (@shift) = @_;
    
    # transform vertex coordinates
    foreach my $vertex (@{$self->vertices}) {
        $vertex->[$_] += $shift[$_] for X,Y,Z;
    }
}

sub align_to_origin {
    my $self = shift;
    
    # calculate the displacements needed to 
    # have lowest value for each axis at coordinate 0
    my @extents = $self->bounding_box;
    $self->move(map -$extents[$_][MIN], X,Y,Z);
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

sub bounding_box {
    my $self = shift;
    my @extents = (map [undef, undef], X,Y,Z);
    foreach my $vertex (@{$self->vertices}) {
        for (X,Y,Z) {
            $extents[$_][MIN] = $vertex->[$_] if !defined $extents[$_][MIN] || $vertex->[$_] < $extents[$_][MIN];
            $extents[$_][MAX] = $vertex->[$_] if !defined $extents[$_][MAX] || $vertex->[$_] > $extents[$_][MAX];
        }
    }
    return @extents;
}

sub size {
    my $self = shift;
    
    my @extents = $self->bounding_box;
    return map $extents[$_][MAX] - $extents[$_][MIN], (X,Y,Z);
}

sub slice_facet {
    my $self = shift;
    my ($print, $facet_id) = @_;
    my ($normal, @vertices) = @{$self->facets->[$facet_id]};
    Slic3r::debugf "\n==> FACET %d (%f,%f,%f - %f,%f,%f - %f,%f,%f):\n",
        $facet_id, map @{$self->vertices->[$_]}, @vertices
        if $Slic3r::debug;
    
    # find the vertical extents of the facet
    my ($min_z, $max_z) = (99999999999, -99999999999);
    foreach my $vertex (@vertices) {
        my $vertex_z = $self->vertices->[$vertex][Z];
        $min_z = $vertex_z if $vertex_z < $min_z;
        $max_z = $vertex_z if $vertex_z > $max_z;
    }
    Slic3r::debugf "z: min = %.0f, max = %.0f\n", $min_z, $max_z;
    
    if (abs($max_z - $min_z) < epsilon) {
        Slic3r::debugf "Facet is horizontal; ignoring\n";
        return;
    }
    
    # calculate the layer extents
    # (the -1 and +1 here are used as a quick and dirty replacement for some
    # complex calculation of the first layer height ratio logic)
    my $min_layer = int($min_z * $Slic3r::resolution / $Slic3r::layer_height) - 1;
    $min_layer = 0 if $min_layer < 0;
    my $max_layer = int($max_z * $Slic3r::resolution / $Slic3r::layer_height) + 1;
    Slic3r::debugf "layers: min = %s, max = %s\n", $min_layer, $max_layer;
    
    for (my $layer_id = $min_layer; $layer_id <= $max_layer; $layer_id++) {
        my $layer = $print->layer($layer_id);
        $layer->add_line($_) for $self->intersect_facet($facet_id, $layer->slice_z);
    }
}

sub intersect_facet {
    my $self = shift;
    my ($facet_id, $z) = @_;
    
    my @vertices_ids        = @{$self->facets->[$facet_id]}[1..3];
    my @edge_ids            = @{$self->facets_edges->[$facet_id]};
    my @edge_vertices_ids   = $self->_facet_edges($facet_id);
    
    my (@lines, @points, @intersection_points, @points_on_layer) = ();
        
    for my $e (0..2) {
        my $edge_id         = $edge_ids[$e];
        my ($a_id, $b_id)   = @{$edge_vertices_ids[$e]};
        my ($a, $b)         = map $self->vertices->[$_], ($a_id, $b_id);
        #printf "Az = %f, Bz = %f, z = %f\n", $a->[Z], $b->[Z], $z;
        
        #if (abs($a->[Z] - $b->[Z]) < epsilon && abs($a->[Z] - $z) < epsilon) {
        if ($a->[Z] == $b->[Z] && $a->[Z] == $z) {
            # edge is horizontal and belongs to the current layer
            my $edge_type = (grep $self->vertices->[$_][Z] < $z, @vertices_ids) ? 'top' : 'bottom';
            if ($edge_type eq 'top') {
                ($a, $b) = ($b, $a);
                ($a_id, $b_id) = ($b_id, $a_id);
            }
            push @lines, Slic3r::TriangleMesh::IntersectionLine->new(
                a           => [$a->[X], $a->[Y]],
                b           => [$b->[X], $b->[Y]],
                a_id        => $a_id,
                b_id        => $b_id,
                facet_edge  => $edge_type,
                facet_index => $facet_id,
            );
            #print "Horizontal edge at $z!\n";
            
        } elsif ($a->[Z] == $z) {
            #print "A point on plane $z!\n";
            push @points, [ $a->[X], $a->[Y], $a_id ];
            push @points_on_layer, $#points;
            
        } elsif ($b->[Z] == $z) {
            #print "B point on plane $z!\n";
            push @points, [ $b->[X], $b->[Y], $b_id ];
            push @points_on_layer, $#points;
            
        } elsif (($a->[Z] < $z && $b->[Z] > $z) || ($b->[Z] < $z && $a->[Z] > $z)) {
            # edge intersects the current layer; calculate intersection
            push @points, [
                $b->[X] + ($a->[X] - $b->[X]) * ($z - $b->[Z]) / ($a->[Z] - $b->[Z]),
                $b->[Y] + ($a->[Y] - $b->[Y]) * ($z - $b->[Z]) / ($a->[Z] - $b->[Z]),
                undef,
                $edge_id,
            ];
            push @intersection_points, $#points;
            #print "Intersects at $z!\n";
        }
    }
    
    return @lines if @lines;
    if (@points_on_layer == 2 && @intersection_points == 1) {
        $points[ $points_on_layer[1] ] = undef;
        @points = grep $_, @points;
    }
    if (@points_on_layer == 2 && @intersection_points == 0) {
        if (same_point(map $points[$_], @points_on_layer)) {
            return ();
        }
    }
    
    if (@points) {
        
        # defensive programming:
        die "Facets must intersect each plane 0 or 2 times" if @points != 2;
        
        # connect points:
        my ($prev_facet_index, $next_facet_index) = (undef, undef);
        $prev_facet_index = +(grep $_ != $facet_id, @{$self->edges_facets->[$points[B][3]]})[0]
            if defined $points[B][3];
        $next_facet_index = +(grep $_ != $facet_id, @{$self->edges_facets->[$points[A][3]]})[0]
            if defined $points[A][3];
        
        return Slic3r::TriangleMesh::IntersectionLine->new(
            a      => [$points[B][X], $points[B][Y]],
            b      => [$points[A][X], $points[A][Y]],
            a_id   => $points[B][2],
            b_id   => $points[A][2],
            facet_index => $facet_id,
            prev_edge_id => $points[B][3],
            next_edge_id => $points[A][3],
            prev_facet_index => $prev_facet_index,
            next_facet_index => $next_facet_index,
        );
        #printf "  intersection points at z = %f: %f,%f - %f,%f\n", $z, map @$_, @intersection_points;
    }
    
    return ();
}

sub get_connected_facets {
    my $self = shift;
    my ($facet_id) = @_;
    
    my %facets = ();
    foreach my $edge_id (@{$self->facets_edges->[$facet_id]}) {
        $facets{$_} = 1 for @{$self->edges_facets->[$edge_id]};
    }
    delete $facets{$facet_id};
    return keys %facets;
}

1;
