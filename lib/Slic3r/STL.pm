package Slic3r::STL;
use Moo;

use CAD::Format::STL;
use Math::Clipper qw(integerize_coordinate_sets is_counter_clockwise);
use Slic3r::Geometry qw(three_points_aligned longest_segment);
use XXX;

use constant X => 0;
use constant Y => 1;
use constant Z => 2;
use constant MIN => 0;
use constant MAX => 1;

sub parse_file {
    my $self = shift;
    my ($file) = @_;
    
    # open STL file
    my $stl = CAD::Format::STL->new->load($file);
    
    if ($Slic3r::rotate > 0) {
        my $deg = Slic3r::Geometry::deg2rad($Slic3r::rotate);
        foreach my $facet ($stl->part->facets) {
            my ($normal, @vertices) = @$facet;
            foreach my $vertex (@vertices) {
                @$vertex = (@{ +(Slic3r::Geometry::rotate_points($deg, undef, [ $vertex->[X], $vertex->[Y] ]))[0] }, $vertex->[Z]);
            }
        }
    }
    
    # we only want to work with positive coordinates, so let's 
    # find our object extents to calculate coordinate displacements
    my @extents = (map [99999999999, -99999999999], X,Y,Z);
    foreach my $facet ($stl->part->facets) {
        my ($normal, @vertices) = @$facet;
        foreach my $vertex (@vertices) {
            for (X,Y,Z) {
                $extents[$_][MIN] = $vertex->[$_] if $vertex->[$_] < $extents[$_][MIN];
                $extents[$_][MAX] = $vertex->[$_] if $vertex->[$_] > $extents[$_][MAX];
            }
        }
    }
    
    # scale extents
    for (X,Y,Z) {
        $extents[$_][MIN] *= $Slic3r::scale;
        $extents[$_][MAX] *= $Slic3r::scale;
    }
    
    # multiply object
    my @multiply_offset = (
        (($extents[X][MAX] - $extents[X][MIN]) + $Slic3r::multiply_distance),
        (($extents[Y][MAX] - $extents[Y][MIN]) + $Slic3r::multiply_distance),
    );
    $extents[X][MAX] += $multiply_offset[X] * ($Slic3r::multiply_x-1);
    $extents[Y][MAX] += $multiply_offset[Y] * ($Slic3r::multiply_y-1);
    my @copies = ();
    for (my $i = 0; $i < $Slic3r::multiply_x; $i++) {
        for (my $j = 0; $j < $Slic3r::multiply_y; $j++) {
            push @copies, [ $multiply_offset[X] * $i, $multiply_offset[Y] * $j ];
        }
    }
    
    # initialize print job
    my $print = Slic3r::Print->new(
        x_length => ($extents[X][MAX] - $extents[X][MIN]) / $Slic3r::resolution,
        y_length => ($extents[Y][MAX] - $extents[Y][MIN]) / $Slic3r::resolution,
    );
    
    # calculate the displacements needed to 
    # have lowest value for each axis at coordinate 0
    my @shift = map sprintf('%.0f', -$extents[$_][MIN] / $Slic3r::resolution), X,Y,Z;
    
    # process facets
    foreach my $facet ($stl->part->facets) {
        
        # transform vertex coordinates
        my ($normal, @vertices) = @$facet;
        foreach my $vertex (@vertices) {
            $vertex->[$_] = ($Slic3r::scale * $vertex->[$_] / $Slic3r::resolution) + $shift[$_]
                for X,Y,Z;
            
            # round Z coordinates to the nearest multiple of layer height
            # XY will be rounded automatically to integers with coercion
            $vertex->[Z] = int($vertex->[Z] * $Slic3r::resolution / $Slic3r::layer_height)
                * $Slic3r::layer_height / $Slic3r::resolution;
        }
        
        foreach my $copy (@copies) {
            my @copy_vertices = map [ @$_ ], @vertices;  # clone vertices
            foreach my $vertex (@copy_vertices) {
                $vertex->[$_] += $copy->[$_] / $Slic3r::resolution for X,Y;
            }
            $self->_facet($print, $normal, @copy_vertices);
        }
    }
    
    return $print;
}

sub _facet {
    my $self = shift;
    my ($print, $normal, @vertices) = @_;
    Slic3r::debugf "\n==> FACET (%f,%f,%f - %f,%f,%f - %f,%f,%f):\n", map @$_, @vertices
        if $Slic3r::debug;
    
    # find the vertical extents of the facet
    my ($min_z, $max_z) = (99999999999, -99999999999);
    foreach my $vertex (@vertices) {
        $min_z = $vertex->[Z] if $vertex->[Z] < $min_z;
        $max_z = $vertex->[Z] if $vertex->[Z] > $max_z;
    }
    Slic3r::debugf "z: min = %.0f, max = %.0f\n", $min_z, $max_z;
    
    # calculate the layer extents
    my $min_layer = int($min_z * $Slic3r::resolution / $Slic3r::layer_height);
    my $max_layer = int(0.99999 + ($max_z * $Slic3r::resolution / $Slic3r::layer_height));
    
    Slic3r::debugf "layers: min = %s, max = %s\n", $min_layer, $max_layer;
    
    # reorder vertices so that the first one is the one with lowest Z
    # this is needed to get all intersection lines in a consistent order
    # (external on the right of the line)
    {
        my @z_order = sort { $vertices[$a][Z] <=> $vertices[$b][Z] } 0..2;
        @vertices = (splice(@vertices, $z_order[0]), splice(@vertices, 0, $z_order[0]));
    }
    
    # is the facet horizontal?
    # (note that we can have $min_z == $max_z && $min_layer != $max_layer
    # if $min_z % $layer_height != 0)
    if ($min_z == $max_z) {
        my $layer = $print->layer($min_layer);
        
        # if all vertices are aligned, then facet is not horizontal but vertical
        # with a height less than layer height: that's why it was squashed on a
        # single layer
        ##local $Slic3r::Geometry::parallel_degrees_limit = 1;
        ##if (three_points_aligned(@vertices)) {
        if (0 && abs($normal->[Z]) == 0) {
            Slic3r::debugf "Facet is vertical with a height less than layer height\n";
            
            my ($p1, $p2, $p3) = @vertices;
            $layer->add_line(Slic3r::Line::FacetEdge->cast(
                $_,
                edge_type => 'bottom',
            )) for ([$p1, $p2], [$p2, $p3], [$p1, $p3], [$p2, $p1], [$p3, $p2], [$p3, $p1]);
            
            return;
        }
        
        Slic3r::debugf "Facet is horizontal\n";
        my $surface = $layer->add_surface(@vertices);
        
        # to determine whether the surface is a top or bottom let's recompute
        # the normal using the right-hand rule
        # (this relies on the STL to be well-formed)
        # recompute the normal using the right-hand rule
        my $vertices_p = [@vertices];
        integerize_coordinate_sets($vertices_p);
        my $clockwise = !is_counter_clockwise($vertices_p);
        
        # defensive programming and/or input check
        if (abs($normal->[Z]) == 1) {
            # while the vertices may belong to the same layer, it doesn't mean the facet
            # was horizontal in the original model; so this check makes sense only 
            # if the original normal is exactly 1 or -1
            if (($normal->[Z] > 0 && $clockwise) || ($normal->[Z] < 0 && !$clockwise)) {
                YYY $normal;
                die sprintf "STL normal (%.0f) and right-hand rule computation (%s) differ!\n",
                    $normal->[Z], $clockwise ? 'clockwise' : 'counter-clockwise';
            }
        }
        
        if ($layer->id == 0 && !$clockwise) {
            YYY $normal;
            die "Right-hand rule gives bad result for facets on base layer!\n";
        }
        
        $surface->surface_type($clockwise ? 'bottom' : 'top');
        
        return;
    }
    
    for (my $layer_id = $min_layer; $layer_id <= $max_layer; $layer_id++) {
        my $layer = $print->layer($layer_id);
        $layer->add_line($_) for $self->intersect_facet(\@vertices, $layer->z);
    }
}

sub intersect_facet {
    my $self = shift;
    my ($vertices, $z) = @_;
    
    # build the three segments of the triangle facet
    my @edges = (
        [ $vertices->[0], $vertices->[1] ],
        [ $vertices->[1], $vertices->[2] ],
        [ $vertices->[2], $vertices->[0] ],
    );
    
    my (@lines, @intersection_points) = ();
        
    foreach my $edge (@edges) {
        my ($a, $b) = @$edge;
        #printf "Az = %d, Bz = %d, z = %d\n", $a->[Z], $b->[Z], $z;
        
        if ($a->[Z] == $b->[Z] && $a->[Z] == $z) {
            # edge is horizontal and belongs to the current layer
            my $edge_type = (grep $_->[Z] > $z, @$vertices) ? 'bottom' : 'top';
            ($a, $b) = ($b, $a) if $edge_type eq 'bottom';
            push @lines, Slic3r::Line::FacetEdge->cast(
                [ [$a->[X], $a->[Y]], [$b->[X], $b->[Y]] ],
                edge_type => $edge_type,
            );
            #print "Horizontal edge!\n";
            
        } elsif (($a->[Z] < $z && $b->[Z] > $z) || ($b->[Z] < $z && $a->[Z] > $z)) {
            # edge intersects the current layer; calculate intersection
            push @intersection_points, [
                $b->[X] + ($a->[X] - $b->[X]) * ($z - $b->[Z]) / ($a->[Z] - $b->[Z]),
                $b->[Y] + ($a->[Y] - $b->[Y]) * ($z - $b->[Z]) / ($a->[Z] - $b->[Z]),
            ];
            #print "Intersects!\n";
            
        } elsif ($a->[Z] == $z) {
            #print "A point on plane!\n";
            push @intersection_points, [ $a->[X], $a->[Y] ];
            
        } elsif ($b->[Z] == $z) {
            #print "B point on plane!\n";
            push @intersection_points, [ $b->[X], $b->[Y] ];
        }
    }
    
    Slic3r::Geometry::remove_coinciding_points(\@intersection_points);
    
    if (@intersection_points > 1 && !@lines) {
        
        # remove coinciding points
        
        # defensive programming:
        die "Facets must intersect each plane 0 or 2 times" if @intersection_points != 2;
        
        # check whether the two points coincide due to resolution rounding
        #if ($intersection_points[0]->coincides_with($intersection_points[1])) {
        #    Slic3r::debugf "Points coincide; removing\n";
        #    return;
        #}
        
        # connect points:
        push @lines, Slic3r::Line->cast([ @intersection_points ]);
    }
    
    return @lines;
}

1;
