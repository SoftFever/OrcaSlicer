package Slic3r::STL;
use Moo;

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
    my $facets = $self->read_file($file);
    
    if ($Slic3r::rotate > 0) {
        my $deg = Slic3r::Geometry::deg2rad($Slic3r::rotate);
        foreach my $facet (@$facets) {
            my ($normal, @vertices) = @$facet;
            foreach my $vertex (@vertices) {
                @$vertex = (@{ +(Slic3r::Geometry::rotate_points($deg, undef, [ $vertex->[X], $vertex->[Y] ]))[0] }, $vertex->[Z]);
            }
        }
    }
    
    # we only want to work with positive coordinates, so let's 
    # find our object extents to calculate coordinate displacements
    my @extents = (map [99999999999, -99999999999], X,Y,Z);
    foreach my $facet (@$facets) {
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
    foreach my $facet (@$facets) {
        
        # transform vertex coordinates
        my ($normal, @vertices) = @$facet;
        foreach my $vertex (@vertices) {
            $vertex->[$_] = ($Slic3r::scale * $vertex->[$_] / $Slic3r::resolution) + $shift[$_]
                for X,Y,Z;
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
    
    if ($min_z == $max_z) {
        Slic3r::debugf "Facet is horizontal; ignoring\n";
        return;
    }
    
    # calculate the layer extents
    my $min_layer = int($min_z * $Slic3r::resolution / $Slic3r::layer_height);
    my $max_layer = int($max_z * $Slic3r::resolution / $Slic3r::layer_height);
    Slic3r::debugf "layers: min = %s, max = %s\n", $min_layer, $max_layer;
    
    # reorder vertices so that the first one is the one with lowest Z
    # this is needed to get all intersection lines in a consistent order
    # (external on the right of the line)
    {
        my @z_order = sort { $vertices[$a][Z] <=> $vertices[$b][Z] } 0..2;
        @vertices = (splice(@vertices, $z_order[0]), splice(@vertices, 0, $z_order[0]));
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
            #print "Horizontal edge at $z!\n";
            
        } elsif (($a->[Z] < $z && $b->[Z] > $z) || ($b->[Z] < $z && $a->[Z] > $z)) {
            # edge intersects the current layer; calculate intersection
            push @intersection_points, [
                $b->[X] + ($a->[X] - $b->[X]) * ($z - $b->[Z]) / ($a->[Z] - $b->[Z]),
                $b->[Y] + ($a->[Y] - $b->[Y]) * ($z - $b->[Z]) / ($a->[Z] - $b->[Z]),
            ];
            #print "Intersects at $z!\n";
            
        } elsif ($a->[Z] == $z) {
            #print "A point on plane $z!\n";
            push @intersection_points, [ $a->[X], $a->[Y] ];
            
        } elsif ($b->[Z] == $z) {
            #print "B point on plane $z!\n";
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
        #printf "  intersection points = %f,%f - %f,%f\n", map @$_, @intersection_points;
    }
    
    return @lines;
}

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    open my $fh, '<', $file or die "Failed to open $file\n";
    my $facets = [];
    
    # let's detect whether file is ASCII or binary
    my $mode;
    {
        my $size = +(stat $fh)[7];
        $mode = 'ascii' if $size < 80 + 4;
        
        # skip binary header
        seek $fh, 80, 0;
        read $fh, my $buf, 4;
        my $triangle_count = unpack 'L', $buf;
        my $expected_size =
            + 80 # header
            +  4 # count
            + $triangle_count * (
                + 4   # normal, pt,pt,pt (vectors)
                  * 4   # bytes per value
                  * 3   # values per vector
                + 2 # the trailing 'short'
            );
        $mode = ($size == $expected_size) ? 'binary' : 'ascii';
    }
    
    $mode eq 'ascii'
        ? _read_ascii($fh, $facets)
        : _read_binary($fh, $facets);
    
    close $fh;
    return $facets;
}

sub _read_ascii {
    my ($fh, $facets) = @_;
    
    my $point_re = qr/([^ ]+)\s+([^ ]+)\s+([^ ]+)$/;
    
    my $facet;
    seek $fh, 0, 0;
    while (<$fh>) {
        chomp;
        if (!$facet) {
            /^\s*facet\s+normal\s+$point_re/ or next;
            $facet = [ [$1, $2, $3] ];
        } else {
            if (/^\s*endfacet/) {
                push @$facets, $facet;
                undef $facet;
            } else {
                /^\s*vertex\s+$point_re/ or next;
                push @$facet, [$1, $2, $3];
            }
        }
    }
}

sub _read_binary {
    my ($fh, $facets) = @_;
    
    die "bigfloat" unless length(pack "f", 1) == 4;
    
    seek $fh, 80 + 4, 0;
    while (read $fh, $_, 4*4*3+2) {
        my @v = unpack '(f3)4';
        push @$facets, [ [@v[0..2]], [@v[3..5]], [@v[6..8]], [@v[9..11]] ];
    }
}

1;
