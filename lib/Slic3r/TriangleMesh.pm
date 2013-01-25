package Slic3r::TriangleMesh;
use Moo;

use Slic3r::Geometry qw(X Y Z A B unscale same_point);
use Slic3r::Geometry::Clipper qw(union_ex);

# public
has 'vertices'      => (is => 'ro', required => 1);         # id => [$x,$y,$z]
has 'facets'        => (is => 'ro', required => 1);         # id => [ $v1_id, $v2_id, $v3_id ]

# private
has 'edges'         => (is => 'ro', default => sub { [] }); # id => [ $v1_id, $v2_id ]
has 'facets_edges'  => (is => 'ro', default => sub { [] }); # id => [ $e1_id, $e2_id, $e3_id ]
has 'edges_facets'  => (is => 'ro', default => sub { [] }); # id => [ $f1_id, $f2_id, (...) ]

use constant MIN => 0;
use constant MAX => 1;

use constant I_FMT              => 'ffllLllc';
use constant I_B                => 0;
use constant I_A_ID             => 1;
use constant I_B_ID             => 2;
use constant I_FACET_INDEX      => 3;
use constant I_PREV_FACET_INDEX => 4;
use constant I_NEXT_FACET_INDEX => 5;
use constant I_FACET_EDGE       => 6;

use constant FE_TOP             => 0;
use constant FE_BOTTOM          => 1;

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
            my @z_order = sort { $self->vertices->[$facet->[$a]][Z] <=> $self->vertices->[$facet->[$b]][Z] } -3..-1;
            @$facet[-3..-1] = (@$facet[$z_order[0]..-1], @$facet[-3..($z_order[0]-1)]);
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
    my $self = shift;
    return (ref $self)->new(
        vertices => [ map [ @$_ ], @{$self->vertices} ],
        facets   => [ map [ @$_ ], @{$self->facets} ],
    );
}

sub _facet_edges {
    my $self = shift;
    my ($facet_id) = @_;
    
    my $facet = $self->facets->[$facet_id];
    return (
        [ $facet->[-3], $facet->[-2] ],
        [ $facet->[-2], $facet->[-1] ],
        [ $facet->[-1], $facet->[-3] ],
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
    
    # look for any edges not connected to exactly two facets
    my ($first_bad_edge_id) =
        grep { @{ $self->edges_facets->[$_] } != 2 } 0..$#{$self->edges_facets};
    if (defined $first_bad_edge_id) {
        warn sprintf "Warning: The input file contains a hole near edge %f,%f,%f-%f,%f,%f (not manifold). "
            . "You might want to repair it and retry, or to check the resulting G-code before printing anyway.\n",
            map @{$self->vertices->[$_]}, @{$self->edges->[$first_bad_edge_id]};
        return 0;
    }
    return 1;
}

sub unpack_line {
    my ($packed) = @_;
    
    my @data = unpack I_FMT, $packed;
    splice @data, 0, 2, [ @data[0,1] ];
    $data[$_] = undef for grep $data[$_] == -1, I_A_ID, I_B_ID, I_FACET_EDGE, I_PREV_FACET_INDEX, I_NEXT_FACET_INDEX;
    return [@data];
}

sub make_loops {
    my ($lines) = @_;
    my @lines = map unpack_line($_), @$lines;
    
    # remove tangent edges
    {
        for (my $i = 0; $i <= $#lines; $i++) {
            next unless defined $lines[$i] && defined $lines[$i][I_FACET_EDGE];
            # if the line is a facet edge, find another facet edge
            # having the same endpoints but in reverse order
            for (my $j = $i+1; $j <= $#lines; $j++) {
                next unless defined $lines[$j] && defined $lines[$j][I_FACET_EDGE];
                
                # are these facets adjacent? (sharing a common edge on this layer)
                if ($lines[$i][I_A_ID] == $lines[$j][I_B_ID] && $lines[$i][I_B_ID] == $lines[$j][I_A_ID]) {
                
                    # if they are both oriented upwards or downwards (like a 'V')
                    # then we can remove both edges from this layer since it won't 
                    # affect the sliced shape
                    if ($lines[$j][I_FACET_EDGE] == $lines[$i][I_FACET_EDGE]) {
                        $lines[$i] = undef;
                        $lines[$j] = undef;
                        last;
                    }
                    
                    # if one of them is oriented upwards and the other is oriented
                    # downwards, let's only keep one of them (it doesn't matter which
                    # one since all 'top' lines were reversed at slicing)
                    if ($lines[$i][I_FACET_EDGE] == FE_TOP && $lines[$j][I_FACET_EDGE] == FE_BOTTOM) {
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
        if (defined $line->[I_PREV_FACET_INDEX]) {
            $prev_count{$line->[I_PREV_FACET_INDEX]}++;
        }
        if (defined $line->[I_A_ID]) {
            $a_count{$line->[I_A_ID]}++;
        }
    }
    
    foreach my $point_id (grep $a_count{$_} > 1, keys %a_count) {
        my @lines_starting_here = grep defined $_->[I_A_ID] && $_->[I_A_ID] == $point_id, @lines;
        Slic3r::debugf "%d lines start at point %d\n", scalar(@lines_starting_here), $point_id;
        
        # if two lines start at this point, one being a 'top' facet edge and the other being a 'bottom' one,
        # then remove the top one and those following it (removing the top or the bottom one is an arbitrary
        # choice)
        # The "// ''" on the next line avoids uninitialized value errors mentioned in issue #357 but these
        # errors occur on fixed models so the root cause still needs to be found
        if (@lines_starting_here == 2 && join('', sort map $_->[I_FACET_EDGE] // '', @lines_starting_here) eq FE_TOP.FE_BOTTOM) { #/
            my @to_remove = grep $_->[I_FACET_EDGE] == FE_TOP, @lines_starting_here;
            while (!grep defined $_->[I_B_ID] && $_->[I_B_ID] == $to_remove[-1]->[I_B_ID] && $_ ne $to_remove[-1], @lines) {
                push @to_remove, grep defined $_->[I_A_ID] && $_->[I_A_ID] == $to_remove[-1]->[I_B_ID], @lines;
            }
            my %to_remove = map {$_ => 1} @to_remove;
            @lines = grep !$to_remove{$_}, @lines;
        } else {
            Slic3r::debugf "  this shouldn't happen and should be further investigated\n";
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output("same_point.svg",
                    lines       => [ map $_->line, grep !defined $_->[I_FACET_EDGE], @lines ],
                    red_lines   => [ map $_->line, grep defined $_->[I_FACET_EDGE], @lines ],
                    #points      => [ $self->vertices->[$point_id] ],
                    no_arrows => 0,
                );
            }
        }
    }
    
    # optimization: build indexes of lines
    my %by_facet_index = map { $lines[$_][I_FACET_INDEX] => $_ }
        grep defined $lines[$_][I_FACET_INDEX],
        (0..$#lines);
    my %by_a_id = map { $lines[$_][I_A_ID] => $_ }
        grep defined $lines[$_][I_A_ID],
        (0..$#lines);
    
    my (@polygons, @failed_loops, %visited_lines) = ();
    my $slicing_errors = 0;
    CYCLE: for (my $i = 0; $i <= $#lines; $i++) {
        my $line = $lines[$i];
        next if $visited_lines{$line};
        my @points = ();
        my $first_facet_index = $line->[I_FACET_INDEX];
        
        do {
            my $next_line;
            if (defined $line->[I_NEXT_FACET_INDEX] && exists $by_facet_index{$line->[I_NEXT_FACET_INDEX]}) {
                $next_line = $lines[$by_facet_index{$line->[I_NEXT_FACET_INDEX]}];
            } elsif (defined $line->[I_B_ID] && exists $by_a_id{$line->[I_B_ID]}) {
                $next_line = $lines[$by_a_id{$line->[I_B_ID]}];
            } else {
                Slic3r::debugf "  line has no next_facet_index or b_id\n";
                $slicing_errors = 1;
                push @failed_loops, [@points] if @points;
                next CYCLE;
            }
            
            if (!$next_line || $visited_lines{$next_line}) {
                Slic3r::debugf "  failed to close this loop\n";
                $slicing_errors = 1;
                push @failed_loops, [@points] if @points;
                next CYCLE;
            } elsif (defined $next_line->[I_PREV_FACET_INDEX] && $next_line->[I_PREV_FACET_INDEX] != $line->[I_FACET_INDEX]) {
                Slic3r::debugf "  wrong prev_facet_index\n";
                $slicing_errors = 1;
                push @failed_loops, [@points] if @points;
                next CYCLE;
            } elsif (defined $next_line->[I_A_ID] && $next_line->[I_A_ID] != $line->[I_B_ID]) {
                Slic3r::debugf "  wrong a_id\n";
                $slicing_errors = 1;
                push @failed_loops, [@points] if @points;
                next CYCLE;
            }
            
            push @points, $next_line->[I_B];
            $visited_lines{$next_line} = 1;
            $line = $next_line;
        } while ($first_facet_index != $line->[I_FACET_INDEX]);
    
        push @polygons, Slic3r::Polygon->new(@points);
        Slic3r::debugf "  Discovered %s polygon of %d points\n",
            ($polygons[-1]->is_counter_clockwise ? 'ccw' : 'cw'), scalar(@points)
            if $Slic3r::debug;
    }
    
    # TODO: we should try to combine failed loops
    for (grep @$_ >= 3, @failed_loops) {
        push @polygons, Slic3r::Polygon->new(@$_);
        Slic3r::debugf "  Discovered failed %s polygon of %d points\n",
            ($polygons[-1]->is_counter_clockwise ? 'ccw' : 'cw'), scalar(@$_)
            if $Slic3r::debug;
    }
    
    return ($slicing_errors, [@polygons]);
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
        $vertex->[$_] += $shift[$_] || 0 for X,Y,Z;
    }
}

sub align_to_origin {
    my $self = shift;
    
    # calculate the displacements needed to 
    # have lowest value for each axis at coordinate 0
    my @extents = $self->extents;
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

sub extents {
    my $self = shift;
    return Slic3r::Geometry::bounding_box_3D($self->vertices);
}

sub size {
    my $self = shift;
    return Slic3r::Geometry::size_3D($self->vertices);
}

sub slice_facet {
    my $self = shift;
    my ($print_object, $facet_id) = @_;
    my @vertices = @{$self->facets->[$facet_id]}[-3..-1];
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
    
    if ($max_z == $min_z) {
        Slic3r::debugf "Facet is horizontal; ignoring\n";
        return;
    }
    
    # calculate the layer extents
    my ($min_layer, $max_layer) = $print_object->get_layer_range($min_z, $max_z);
    Slic3r::debugf "layers: min = %s, max = %s\n", $min_layer, $max_layer;
    
    my $lines = {};  # layer_id => [ lines ]
    for (my $layer_id = $min_layer; $layer_id <= $max_layer; $layer_id++) {
        my $layer = $print_object->layers->[$layer_id];
        $lines->{$layer_id} ||= [];
        push @{ $lines->{$layer_id} }, $self->intersect_facet($facet_id, $layer->slice_z);
    }
    return $lines;
}

sub intersect_facet {
    my $self = shift;
    my ($facet_id, $z) = @_;
    
    my @vertices_ids        = @{$self->facets->[$facet_id]}[-3..-1];
    my @edge_ids            = @{$self->facets_edges->[$facet_id]};
    my @edge_vertices_ids   = $self->_facet_edges($facet_id);
    
    my (@lines, @points, @intersection_points, @points_on_layer) = ();
        
    for my $e (0..2) {
        my $edge_id         = $edge_ids[$e];
        my ($a_id, $b_id)   = @{$edge_vertices_ids[$e]};
        my ($a, $b)         = map $self->vertices->[$_], ($a_id, $b_id);
        #printf "Az = %f, Bz = %f, z = %f\n", $a->[Z], $b->[Z], $z;
        
        if ($a->[Z] == $b->[Z] && $a->[Z] == $z) {
            # edge is horizontal and belongs to the current layer
            my $edge_type = (grep $self->vertices->[$_][Z] < $z, @vertices_ids) ? FE_TOP : FE_BOTTOM;
            if ($edge_type == FE_TOP) {
                ($a, $b) = ($b, $a);
                ($a_id, $b_id) = ($b_id, $a_id);
            }
            push @lines, pack I_FMT, (
                $b->[X], $b->[Y],       # I_B
                $a_id,                  # I_A_ID
                $b_id,                  # I_B_ID
                $facet_id,              # I_FACET_INDEX
                -1,                     # I_PREV_FACET_INDEX
                -1,                     # I_NEXT_FACET_INDEX
                $edge_type,             # I_FACET_EDGE
                
                # Unused data:
                # a             => [$a->[X], $a->[Y]],
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
        
        return pack I_FMT, (
            $points[A][X], $points[A][Y],   # I_B
            $points[B][2] // -1,            # I_A_ID /
            $points[A][2] // -1,            # I_B_ID /
            $facet_id,                      # I_FACET_INDEX
            $prev_facet_index // -1,        # I_PREV_FACET_INDEX  /
            $next_facet_index // -1,        # I_NEXT_FACET_INDEX  /
            -1,                             # I_FACET_EDGE
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

sub split_mesh {
    my $self = shift;
    
    my @meshes = ();
    
    # loop while we have remaining facets
    while (1) {
        # get the first facet
        my @facet_queue = ();
        my @facets = ();
        for (my $i = 0; $i <= $#{$self->facets}; $i++) {
            if (defined $self->facets->[$i]) {
                push @facet_queue, $i;
                last;
            }
        }
        last if !@facet_queue;
        
        while (defined (my $facet_id = shift @facet_queue)) {
            next unless defined $self->facets->[$facet_id];
            push @facets, map [ @$_ ], $self->facets->[$facet_id];
            push @facet_queue, $self->get_connected_facets($facet_id);
            $self->facets->[$facet_id] = undef;
        }
        
        my %vertices = map { $_ => 1 } map @$_[-3..-1], @facets;
        my @new_vertices = keys %vertices;
        my %new_vertices = map { $new_vertices[$_] => $_ } 0..$#new_vertices;
        foreach my $facet (@facets) {
            $facet->[$_] = $new_vertices{$facet->[$_]} for -3..-1;
        }
        push @meshes, Slic3r::TriangleMesh->new(
            facets => \@facets,
            vertices => [ map $self->vertices->[$_], keys %vertices ],
        );
    }
    
    return @meshes;
}

sub horizontal_projection {
    my $self = shift;
    
    my @f = ();
    foreach my $facet (@{$self->facets}) {
        push @f, Slic3r::Polygon->new([ map [ @{$self->vertices->[$_]}[X,Y] ], @$facet ]);
    }
    
    $_->make_counter_clockwise for @f;
    my $scale_vector = Math::Clipper::integerize_coordinate_sets({ bits => 32 }, @f);
    my $union = union_ex([ Slic3r::Geometry::Clipper::offset(\@f, 10000) ]);
    Math::Clipper::unscale_coordinate_sets($scale_vector, $_) for @$union;
    return $union;
}

1;
