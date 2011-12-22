package Slic3r::TriangleMesh;
use Moo;

use Slic3r::Geometry qw(X Y Z A B PI epsilon same_point points_coincide angle3points
    merge_collinear_lines nearest_point polyline_lines);
use XXX;

has 'facets'        => (is => 'ro', default => sub { [] });
has 'edges'         => (is => 'ro', default => sub { [] });
has 'edge_table'    => (is => 'ro', default => sub { {} });
has 'edge_facets'   => (is => 'ro', default => sub { {} });

use constant MIN => 0;
use constant MAX => 1;

sub make_edge_table {
    my $self = shift;
    
    @{$self->edges} = ();
    %{$self->edge_table} = ();
    %{$self->edge_facets} = ();
    for (my $facet_index = 0; $facet_index <= $#{$self->facets}; $facet_index++) {
        my $facet = $self->facets->[$facet_index];
        foreach my $edge ($self->facet_edges($facet)) {
            my $edge_id = $self->edge_id($edge);
            if (!exists $self->edge_table->{$edge_id}) {
                push @{$self->edges}, $edge;
                $self->edge_table->{$edge_id} = $#{$self->edges};
                $self->edge_facets->{$edge_id} = [];
            }
            my $edge_index = $self->edge_table->{$edge_id};
            push @{$self->edge_facets->{$edge_id}}, $facet_index;
        }
    }
}

sub check_manifoldness {
    my $self = shift;
    
    $self->make_edge_table;
    
    if (grep { @$_ != 2 } values %{$self->edge_facets}) {
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
                next unless defined $lines[$j] && defined $lines[$j]->facet_edge;
                next unless $lines[$j]->facet_edge eq $lines[$i]->facet_edge;
                if (same_point($lines[$i]->a, $lines[$j]->b) && same_point($lines[$i]->b, $lines[$j]->a)) {
                    $lines[$j] = undef;
                    last;
                }
            }
        }
    }
    
    my $sparse_lines = [ map $_->line, grep $_, @lines ];
    
    # detect closed loops
    
    my (@polygons, %visited_lines, @discarded_lines, @discarded_polylines) = ();
    
    my $detect = sub {
        my @lines = @$sparse_lines;
        (@polygons, %visited_lines, @discarded_lines, @discarded_polylines) = ();
        my $get_point_id = sub { sprintf "%.0f,%.0f", @{$_[0]} };
        
        my (%pointmap, @pointmap_keys) = ();
        foreach my $line (@lines) {
            my $point_id = $get_point_id->($line->[A]);
            if (!exists $pointmap{$point_id}) {
                $pointmap{$point_id} = [];
                push @pointmap_keys, $line->[A];
            }
            push @{ $pointmap{$point_id} }, $line;
        }
        
        my $n = 0;
        while (my $first_line = shift @lines) {
            next if $visited_lines{ $first_line->id };
            my @points = @$first_line;
            
            my @seen_lines = ($first_line);
            my %seen_points = map { $get_point_id->($points[$_]) => $_ } 0..1;
            
            CYCLE: while (1) {
                my $next_lines = $pointmap{ $get_point_id->($points[-1]) };
                
                # shouldn't we find the point, let's try with a slower algorithm
                # as approximation may make the coordinates differ
                if (!$next_lines) {
                    my $nearest_point = nearest_point($points[-1], \@pointmap_keys);
                    #printf "  we have a nearest point: %f,%f (%s)\n", @$nearest_point, $get_point_id->($nearest_point);
                    
                    if ($nearest_point) {
                        local $Slic3r::Geometry::epsilon = 1000000;
                        $next_lines = $pointmap{$get_point_id->($nearest_point)}
                            if points_coincide($points[-1], $nearest_point);
                    }
                }
                
                if (0 && !$next_lines) {
                    require "Slic3r/SVG.pm";
                    Slic3r::SVG::output(undef, "no_lines.svg",
                        lines       => [ grep !$_->isa('Slic3r::Line::FacetEdge'), @lines ],
                        red_lines   => [ grep  $_->isa('Slic3r::Line::FacetEdge'), @lines ],
                        points      => [ $points[-1] ],
                        no_arrows => 1,
                    );
                }
                
                $next_lines
                    or printf("No lines start at point %s. This shouldn't happen. Please check the model for manifoldness.\n", $get_point_id->($points[-1]));
                last CYCLE if !$next_lines or !@$next_lines;
                
                my @ordered_next_lines = sort 
                    { angle3points($points[-1], $points[-2], $next_lines->[$a][B]) <=> angle3points($points[-1], $points[-2], $next_lines->[$b][B]) } 
                    0..$#$next_lines;
                
                #if (@$next_lines > 1) {
                #    Slic3r::SVG::output(undef, "next_line.svg",
                #        lines        => $next_lines,
                #        red_lines    => [ polyline_lines([@points]) ],
                #        green_lines  => [ $next_lines->[ $ordered_next_lines[0] ] ],
                #    );
                #}
                
                my ($next_line) = splice @$next_lines, $ordered_next_lines[0], 1;
                push @seen_lines, $next_line;
                
                push @points, $next_line->[B];
                
                my $point_id = $get_point_id->($points[-1]);
                if ($seen_points{$point_id}) {
                    splice @points, 0, $seen_points{$point_id};
                    last CYCLE;
                }
                
                $seen_points{$point_id} = $#points;
            }
            
            if (@points < 4 || !points_coincide($points[0], $points[-1])) {
                # discarding polyline
                push @discarded_lines, @seen_lines;
                if (@points > 2) {
                    push @discarded_polylines, [@points];
                }
                next;
            }
            
            $visited_lines{ $_->id } = 1 for @seen_lines;
            pop @points;
            Slic3r::debugf "Discovered polygon of %d points\n", scalar(@points);
            push @polygons, Slic3r::Polygon->new(@points);
            $polygons[-1]->cleanup;
        }
    };
    
    $detect->();
    
    # Now, if we got a clean and manifold model then @polygons would contain everything
    # we need to draw our layer. In real life, sadly, things are different and it is likely
    # that the above algorithm wasn't able to detect every polygon. This may happen because
    # of non-manifoldness or because of many close lines, often overlapping; both situations
    # make a head-to-tail search difficult.
    # On the other hand, we can safely assume that every polygon we detected is correct, as 
    # the above algorithm is quite strict. We can take a brute force approach to connect any
    # other line.
    
    # So, let's first check what lines were not detected as part of polygons.
    if (@discarded_lines) {
        Slic3r::debugf "  %d lines out of %d were discarded and %d polylines were not closed\n",
            scalar(@discarded_lines), scalar(@lines), scalar(@discarded_polylines);
        print "  Warning: errors while parsing this layer (dirty or non-manifold model).\n";
        
        my $total_detected_length = 0;
        $total_detected_length += $_->length for map $_->lines, @polygons;
        my $total_discarded_length = 0;
        $total_discarded_length += $_->length for map polyline_lines($_), @discarded_polylines;
        $total_discarded_length += $_->length for @discarded_lines;
        my $discarded_ratio = $total_detected_length
            ? ($total_discarded_length / $total_detected_length)
            : 0;
        
        Slic3r::debugf "  length ratio of discarded lines is %f\n", $discarded_ratio;
        
        if ($discarded_ratio > 0.00001) {
            print "  Retrying with slower algorithm.\n";
            
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output(undef, "layer" . $layer->id . "_detected.svg",
                    white_polygons => \@polygons,
                );
                Slic3r::SVG::output(undef, "layer" . $layer->id . "_discarded_lines.svg",
                    red_lines   => \@discarded_lines,
                );
                Slic3r::SVG::output(undef, "layer" . $layer->id . "_discarded_polylines.svg",
                    polylines   => \@discarded_polylines,
                );
            }
            
            $sparse_lines = merge_collinear_lines($sparse_lines);
            eval { $detect->(); };
            warn $@ if $@;
            
            if (@discarded_lines) {
                print "  Warning: even slow detection algorithm threw errors. Review the output before printing.\n";
                $layer->slicing_errors(1);
            }
        }
    }
    
    return [@polygons];
}

sub rotate {
    my $self = shift;
    my ($deg) = @_;
    return if $deg == 0;
    
    my $rad = Slic3r::Geometry::deg2rad($deg);
    foreach my $facet (@{$self->facets}) {
        my ($normal, @vertices) = @$facet;
        foreach my $vertex (@vertices) {
            @$vertex = (@{ +(Slic3r::Geometry::rotate_points($rad, undef, [ $vertex->[X], $vertex->[Y] ]))[0] }, $vertex->[Z]);
        }
    }
}

sub scale {
    my $self = shift;
    my ($factor) = @_;
    return if $factor == 1;
    
    foreach my $facet (@{$self->facets}) {
        # transform vertex coordinates
        my ($normal, @vertices) = @$facet;
        foreach my $vertex (@vertices) {
            $vertex->[$_] *= $factor for X,Y,Z;
        }
    }
}

sub move {
    my $self = shift;
    my (@shift) = @_;
    
    foreach my $facet (@{$self->facets}) {
        # transform vertex coordinates
        my ($normal, @vertices) = @$facet;
        foreach my $vertex (@vertices) {
            $vertex->[$_] += $shift[$_] for X,Y,Z;
        }
    }
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
                push @{$new_facets[-1]}, [ map $vertex->[$_] + ($shift->[$_] || 0), (X,Y,Z) ];
            }
        }
    }
    push @{$self->facets}, @new_facets;
}

sub bounding_box {
    my $self = shift;
    my @extents = (map [9999999999999999999999, -9999999999999999999999], X,Y,Z);
    foreach my $facet (@{$self->facets}) {
        my ($normal, @vertices) = @$facet;
        foreach my $vertex (@vertices) {
            for (X,Y,Z) {
                $extents[$_][MIN] = $vertex->[$_] if $vertex->[$_] < $extents[$_][MIN];
                $extents[$_][MAX] = $vertex->[$_] if $vertex->[$_] > $extents[$_][MAX];
            }
        }
    }
    return @extents;
}

sub size {
    my $self = shift;
    
    my @extents = $self->bounding_box;
    return map $extents[$_][MAX] - $extents[$_][MIN], (X,Y,Z);
}

sub _facet {
    my $self = shift;
    my ($print, $facet_index, $normal, @vertices) = @_;
    Slic3r::debugf "\n==> FACET %d (%f,%f,%f - %f,%f,%f - %f,%f,%f):\n",
        $facet_index, map @$_, @vertices
        if $Slic3r::debug;
    
    # find the vertical extents of the facet
    my ($min_z, $max_z) = (99999999999, -99999999999);
    foreach my $vertex (@vertices) {
        $min_z = $vertex->[Z] if $vertex->[Z] < $min_z;
        $max_z = $vertex->[Z] if $vertex->[Z] > $max_z;
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
    
    # reorder vertices so that the first one is the one with lowest Z
    # this is needed to get all intersection lines in a consistent order
    # (external on the right of the line)
    {
        my @z_order = sort { $vertices[$a][Z] <=> $vertices[$b][Z] } 0..2;
        @vertices = (splice(@vertices, $z_order[0]), splice(@vertices, 0, $z_order[0]));
    }
    
    for (my $layer_id = $min_layer; $layer_id <= $max_layer; $layer_id++) {
        my $layer = $print->layer($layer_id);
        $layer->add_line($_) for $self->intersect_facet($facet_index, \@vertices, $layer->slice_z);
    }
}

sub intersect_facet {
    my $self = shift;
    my ($facet_index, $vertices, $z) = @_;
    
    # build the three segments of the triangle facet
    my @edges = $self->facet_edges($vertices);
    
    my (@lines, @points, @intersection_points, @points_on_layer) = ();
        
    foreach my $edge (@edges) {
        my ($a, $b) = @$edge;
        my $edge_id = $self->edge_id($edge);
        #printf "Az = %f, Bz = %f, z = %f\n", $a->[Z], $b->[Z], $z;
        
        if (abs($a->[Z] - $b->[Z]) < epsilon && abs($a->[Z] - $z) < epsilon) {
            # edge is horizontal and belongs to the current layer
            my $edge_type = (grep $_->[Z] < $z - epsilon, @$vertices) ? 'top' : 'bottom';
            ($a, $b) = ($b, $a) if $edge_type eq 'top';
            push @lines, Slic3r::TriangleMesh::IntersectionLine->new(
                a           => [$a->[X], $a->[Y]],
                b           => [$b->[X], $b->[Y]],
                a_id        => sprintf("%f,%f", @$a[X,Y]),
                b_id        => sprintf("%f,%f", @$b[X,Y]),
                facet_edge  => $edge_type,
                facet_index => $facet_index,
            );
            #print "Horizontal edge at $z!\n";
            
        } elsif (abs($a->[Z] - $z) < epsilon) {
            #print "A point on plane $z!\n";
            push @points, [ $a->[X], $a->[Y], sprintf("%f,%f", @$a[X,Y]) ];
            push @points_on_layer, $#points;
            
        } elsif (abs($b->[Z] - $z) < epsilon) {
            #print "B point on plane $z!\n";
            push @points, [ $b->[X], $b->[Y], sprintf("%f,%f", @$b[X,Y]) ];
            push @points_on_layer, $#points;
            
        } elsif (($a->[Z] < ($z - epsilon) && $b->[Z] > ($z + epsilon)) 
            || ($b->[Z] < ($z - epsilon) && $a->[Z] > ($z + epsilon))) {
            # edge intersects the current layer; calculate intersection
            push @points, [
                $b->[X] + ($a->[X] - $b->[X]) * ($z - $b->[Z]) / ($a->[Z] - $b->[Z]),
                $b->[Y] + ($a->[Y] - $b->[Y]) * ($z - $b->[Z]) / ($a->[Z] - $b->[Z]),
                $edge_id,
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
        return Slic3r::TriangleMesh::IntersectionLine->new(
            a      => [$points[B][X], $points[B][Y]],
            b      => [$points[A][X], $points[A][Y]],
            a_id   => $points[B][2],
            b_id   => $points[A][2],
            facet_index => $facet_index,
            prev_facet_index => ($points[B][3] ? +(grep $_ != $facet_index, @{$self->edge_facets->{$points[B][3]}})[0] || undef : undef),
            next_facet_index => ($points[A][3] ? +(grep $_ != $facet_index, @{$self->edge_facets->{$points[A][3]}})[0] || undef : undef),
        );
        #printf "  intersection points at z = %f: %f,%f - %f,%f\n", $z, map @$_, @intersection_points;
    }
    
    return ();
}

sub facet_edges {
    my $self = shift;
    my ($facet) = @_;
    
    # ignore the normal if provided
    my @vertices = @$facet[-3..-1];
    
    return (
        [ $vertices[0], $vertices[1] ],
        [ $vertices[1], $vertices[2] ],
        [ $vertices[2], $vertices[0] ],
    )
}

sub edge_id {
    my $self = shift;
    my ($edge) = @_;
    
    my @point_ids = map sprintf("%f,%f,%f", @$_), @$edge;
    return join "-", sort @point_ids;
}

1;
