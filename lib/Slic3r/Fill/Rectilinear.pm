package Slic3r::Fill::Rectilinear;
use Moo;

use constant PI => 4 * atan2(1, 1);
use constant X1 => 0;
use constant Y1 => 1;
use constant X2 => 2;
use constant Y2 => 3;
use constant A => 0;
use constant B => 1;
use constant X => 0;
use constant Y => 1;

use Math::Geometry::Planar;
use POSIX qw(ceil);
use XXX;

sub make_fill {
    my $self = shift;
    my ($print, $layer) = @_;
    printf "Filling layer %d:\n", $layer->id;
    
    my $max_print_dimension = $print->max_length;
    my $n = 1;
    SURFACE: foreach my $surface (@{ $layer->fill_surfaces }) {
        Slic3r::debugf " Processing surface %s:\n", $surface->id;
        my $polygon = $surface->mgp_polygon;
        
        # alternate fill direction
        my (@rotate, @shift);
        if ($layer->id % 2) {
            @rotate = ( PI/2, [ $print->x_length / 2, $print->y_length / 2 ] );
            $shift[X] = $print->y_length / 2 - $print->x_length / 2;
            $shift[Y] = -$shift[X];
        }
        
        # TODO: here we should implement an "infill in direction of bridges" option
        
        # rotate surface as needed
        $polygon = $polygon->rotate(@rotate)->move(@shift) if @rotate;
        
        # force 100% density for external surfaces
        my $density = $surface->surface_type eq 'internal' ? $Slic3r::fill_density : 1;
        next SURFACE unless $density > 0;
        
        my $distance_between_lines = $Slic3r::flow_width / $Slic3r::resolution / $density;
        my $number_of_lines = ceil($max_print_dimension / $distance_between_lines);

        printf "distance = %f\n", $distance_between_lines;
        printf "number_of_lines = %d\n", $number_of_lines;
        
        # this arrayref will hold intersection points of the fill grid with surface segments
        my $points = [ map [], 0..$number_of_lines-1 ];
        foreach my $line (map $self->_lines_from_mgp_points($_), @{ $polygon->polygons }) {
            
            # find out the coordinates
            my @coordinates = map @$_, @$line;
            
            # get the extents of the segment along the primary axis
            my @line_c = sort { $a <=> $b } @coordinates[X1, X2];
            Slic3r::debugf "Segment %d,%d - %d,%d (extents: %f, %f)\n", @coordinates, @line_c;
            
            for (my $c = int($line_c[0] / $distance_between_lines) * $distance_between_lines; 
                    $c <= $line_c[1]; $c += $distance_between_lines) {
                next if $c < $line_c[0] || $c > $line_c[1];
                my $i = sprintf('%.0f', $c / $distance_between_lines) - 1;
                printf "CURRENT \$i = %d, \$c = %f\n", $i, $c;
                
                # if the segment is parallel to our ray, there will be two intersection points
                if ($line_c[0] == $line_c[1]) {
                    Slic3r::debugf "  Segment is parallel!\n";
                    push @{ $points->[$i] }, $coordinates[Y1], $coordinates[Y2];
                    Slic3r::debugf "   intersections at %f (%d) = %f, %f\n", $c, $i, $points->[$i][-2], $points->[$i][-1];
                } else {
                    Slic3r::debugf "  Segment NOT parallel!\n";
                    # one point of intersection
                    push @{ $points->[$i] }, $coordinates[Y1] + ($coordinates[Y2] - $coordinates[Y1])
                        * ($c - $coordinates[X1]) / ($coordinates[X2] - $coordinates[X1]);
                    Slic3r::debugf "   intersection at %f (%d) = %f\n", $c, $i, $points->[$i][-1];
                }
            }
        }
        
        # sort and remove duplicates
        for (my $i = 0; $i <= $#$points; $i++) {
            my %h = map { sprintf("%.9f", $_) => 1 } @{ $points->[$i] };
            $points->[$i] = [ sort { $a <=> $b } keys %h ];
        }
        
        # generate extrusion paths
        my (@paths, @path_points) = ();
        my $direction = 0;
        
        my $stop_path = sub {
            # defensive programming
            if (@path_points == 1) {
                #warn "There shouldn't be only one point in the current path";
            }
                
            # if we were constructing a path, stop it
            push @paths, [ @path_points ] if @path_points > 1;
            @path_points = ();
        };
        
        # loop until we have spare points
        CYCLE: while (scalar map(@$_, @$points) > 1) {
            # loop through rows
            ROW: for (my $i = 0; $i <= $#$points; $i++) {
                my $row = $points->[$i] or next ROW;
                Slic3r::debugf "\nProcessing row %d (direction: %d)...\n", $i, $direction;
                if (!@$row) {
                    Slic3r::debugf "  no points\n";
                    $stop_path->();
                    next ROW;
                }
                Slic3r::debugf "  points = %s\n", join ', ', @$row if $Slic3r::debug;
                
                # coordinate of current row
                my $c = ($i + 1) * $distance_between_lines;
                
                # need to start a path?
                if (!@path_points) {
                    Slic3r::debugf "  path starts at %d\n", $row->[0];
                    push @path_points, [ $c, shift @$row ];
                }
                
                my @search_points = @$row;
                @search_points = reverse @search_points if $direction == 1;
                my @connectable_points = $self->find_connectable_points($polygon, $path_points[-1], $c, [@search_points]);
                Slic3r::debugf "  ==> found %d connectable points = %s\n", scalar(@connectable_points),
                    join ', ', @connectable_points if $Slic3r::debug;
                
                if (!@connectable_points && @path_points && $path_points[-1][0] != $c) {
                    # no connectable in this row
                    $stop_path->();
                }
                
                if (@connectable_points == 1 && $path_points[0][0] != $c 
                    && (($connectable_points[0] == $row->[-1] && $direction == 0)
                        || ($connectable_points[0] == $row->[0] && $direction == 1))) {
                    $i--; # keep searching on current row in the opposite direction
                }
                
                foreach my $p (@connectable_points) {
                    push @path_points, [ $c, $p ];
                    @$row = grep $_ != $p, @$row;  # remove point from row
                }
                
                # invert direction
                $direction = $direction ? 0 : 1;
            }
            $stop_path->() if @path_points;
        }
        
        # paths must be rotated back
        if (@rotate) {
            # TODO: this skips 2-points paths! we shouldn't create a mgp polygon
            @paths = map $self->_mgp_from_points_ref($_)->move(map -$_, @shift)->rotate(-$rotate[0], $rotate[1])->points, @paths;
        }
        
        # save into layer
        FINISH: push @{ $layer->fills }, map Slic3r::ExtrusionPath->cast([ @$_ ]), @paths;
    }
}

# this function will select the first contiguous block of 
# points connectable to a given one
sub find_connectable_points {
    my $self = shift;
    my ($polygon, $point, $c, $points) = @_;
    
    my @connectable_points = ();
    foreach my $p (@$points) {
        if (!$self->can_connect($polygon, $point, [ $c, $p ])) {
             @connectable_points ? last : next;
        }
        push @connectable_points, $p;
        $point = [ $c, $p ] if $point->[0] != $c;
    }
    return @connectable_points;
}

# this subroutine tries to determine whether two points in a surface
# are connectable without crossing contour or holes
sub can_connect {
    my $self = shift;
    my ($polygon, $p1, $p2) = @_;
    printf "  Checking connectability of point %d\n", $p2->[1];
    
    # there's room for optimization here
    
    # this is not needed since we assume that $p1 and $p2 belong to $polygon
    for ($p1, $p2) {
        #return 0 unless $polygon->isinside($_);
        
        # TODO: re-enable this one after testing point_in_polygon() which
        # doesn't detect well points on the contour of polygon
        #return 0 unless Slic3r::Geometry::point_in_polygon($_, $polygon->points);
    }
    
    # check whether the $p1-$p2 segment doesn't intersect any segment
    # of the contour or of holes
    my ($contour_p, @holes_p) = $polygon->get_polygons;
    foreach my $points ($contour_p, @holes_p) {
        foreach my $line ($self->_lines_from_mgp_points($points)) {
            
            # theoretically speaking, SegmentIntersection() would be the right tool for the 
            # job; however floating point math often makes it not return any intersection
            # point between our hypothetical extrusion segment and any other one, even 
            # if, of course, the final point of the extrusion segment is taken from
            # $point and thus it's a point that belongs for sure to a segment.
            # then, let's calculate intersection considering extrusion segment as a ray
            # instead of a segment, and then check whether the intersection point 
            # belongs to the segment
            my $point = SegmentRayIntersection([@$line, $p1, $p2]);
            #printf "    intersecting ray %f,%f - %f,%f and segment %f,%f - %f,%f\n",
            #    @$p1, @$p2, map @$_, @$line;
            
            if ($point && Slic3r::Geometry::line_point_belongs_to_segment($point, [$p1, $p2])) {
                #printf "  ...point intersects!\n";
                #YYY [ $point, $p1, $p2 ];
                
                # our $p1-$p2 line intersects $line
                
                # if the intersection point is an intermediate point of $p1-$p2
                # it means that $p1-$p2 crosses $line, thus we're sure that 
                # $p1 and $p2 are not connectible (one is inside polygon and one
                # is outside), unless $p1-$p2 and $line coincide but we've got
                # an intersection due to floating point math
                my @points_not_belonging_to_line = grep !Slic3r::Geometry::points_coincide($point, $_), $p1, $p2;
                if (@points_not_belonging_to_line == 2) {
                
                    # make sure $p1-$p2 and $line are two distinct lines; we do this
                    # by checking their slopes
                    if (!Slic3r::Geometry::lines_parallel([$p1, $p2], $line)) {
                        #printf "  ...lines cross!\n";
                        #Slic3r::SVG::output_lines($main::print, "lines" . $n++ . ".svg", [ @lines, [$p1, $p2] ]);
                        return 0;
                    }
                    
                }
                
                # defensive programming, this shouldn't happen
                if (@points_not_belonging_to_line == 0) {
                    die "SegmentIntersection is not expected to return an intersection point "
                        . "if \$line coincides with \$p1-\$p2";
                }
                
                # if we're here, then either $p1 or $p2 belong to $line
                # so we have to check whether the other point falls inside
                # the polygon or not
                # we rely on Math::Geometry::Planar returning contour points
                # in counter-clockwise order and hole points in clockwise
                # order, so that if the point falls on the left of $line
                # it's inside the polygon and viceversa
                my $C = $points_not_belonging_to_line[0];
                my $isInside = (($line->[B][X] - $line->[A][X])*($C->[Y] - $line->[A][Y]) 
                    - ($line->[B][Y] - $line->[A][Y])*($C->[X] - $line->[A][X])) > 0;
                
                #printf "  \$line is inside polygon: %d\n", $isInside;
                
                
                # if the line is outside the polygon then points are not connectable
                return 0 if !$isInside;
                #Slic3r::SVG::output_lines($main::print, "lines" . $n++ . ".svg", [ @lines, [$p1, $p2] ])
                #    if !$isInside;
            }
        }
    }

    # even if no intersection is found, we should check whether both $p1 and $p2 are
    # inside a hole; this may happen due to floating point path
    #foreach my $hole_p (map $self->_mgp_from_points_ref($_), @holes_p) {
    #    if ($hole_p->isinside($p1) || $hole_p->isinside($p2)) {
    #        return 0;
    #    }
    #}
    
    #use Slic3r::SVG;
    #Slic3r::SVG::output_lines($main::print, "lines" . $n++ . ".svg", [ @lines, [$p1, $p2] ]);
    
    return 1;
}

sub _lines_from_mgp_points {
    my $self = shift;
    my ($points) = @_;
    
    my @lines = ();
    my $last_point = $points->[-1];
    foreach my $point (@$points) {
        push @lines, [ $last_point, $point ];
        $last_point = $point;
    }
    return @lines;
}

sub _mgp_from_points_ref {
    my $self = shift;
    my ($points) = @_;
    my $p = Math::Geometry::Planar->new;
    $p->points($points);
    return $p;
}

1;
