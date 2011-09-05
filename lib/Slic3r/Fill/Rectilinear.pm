package Slic3r::Fill::Rectilinear;
use Moose;

use constant epsilon => 1E-10;
use constant PI => 4 * atan2(1, 1);
use constant X1 => 0;
use constant Y1 => 1;
use constant X2 => 2;
use constant Y2 => 3;

use Math::Geometry::Planar;
use XXX;

sub make_fill {
    my $self = shift;
    my ($print, $layer) = @_;
    printf "Filling layer %d:\n", $layer->id;
    
    # let's alternate fill direction
    my @axes = $layer->id % 2 == 0 ? (0,1) : (1,0);
    
    foreach my $surface (@{ $layer->fill_surfaces }) {
        Slic3r::debugf " Processing surface %s:\n", $surface->id;
        my $polygon = $surface->mgp_polygon;
        
        # rotate surface as needed
        if ($axes[0] == 1) {
            $polygon = $polygon->rotate(PI/2)->move($print->x_length, $print->y_length);
        }
        
        # force 100% density for external surfaces
        my $density = $surface->surface_type eq 'internal' ? $Slic3r::fill_density : 1;
        my $distance_between_lines = $Slic3r::flow_width / $Slic3r::resolution / $density;
        my $number_of_lines = ($axes[0] == 0 ? $print->x_length : $print->y_length) / $distance_between_lines;
        
        # this arrayref will hold intersection points of the fill grid with surface segments
        my $points = [ map [], 0..$number_of_lines-1 ];
        foreach my $line (map $self->_lines_from_mgp_points($_), @{ $polygon->polygons }) {
        
            # for a possible implementation of "infill in direction of bridges"
            # we should rotate $line so that primary axis is in detected direction;
            # then, generated extrusion paths should be rotated back to the original
            # coordinate system
            
            # find out the coordinates
            my @coordinates = map @$_, @$line;
            Slic3r::debugf "Segment %d,%d - %d,%d\n", @coordinates;
            
            # get the extents of the segment along the primary axis
            my @line_c = sort ($coordinates[X1], $coordinates[X2]);
            
            for (my $c = $line_c[0]; $c <= $line_c[1]; $c += $distance_between_lines) {
                my $i = sprintf('%.0f', $c / $distance_between_lines) - 1;
                
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
        $points = [
            map {
                my %h = map { sprintf("%.0f", $_) => 1 } @$_;
                [ sort keys %h ];
            } @$points
        ];
        
        # generate extrusion paths
        my (@paths, @path_points) = ();
        my $direction = 0;
        
        my $stop_path = sub {
            # defensive programming
            if (@path_points == 1) {
                YYY \@path_points;
                die "There shouldn't be only one point in the current path";
            }
                
            # if we were constructing a path, stop it
            push @paths, [ @path_points ] if @path_points;
            @path_points = ();
        };
        
        # loop until we have spare points
        while (map @$_, @$points) {
        
            # loop through rows
            ROW: for (my $i = 0; $i < $number_of_lines; $i++) {
                my $row = $points->[$i];
                Slic3r::debugf "Processing row %d...\n", $i;
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
                    push @path_points, [ $c, shift @$row ];
                }
                
                my @connectable_points = $self->find_connectable_points($polygon, $path_points[-1], $c, $row);
                @connectable_points = reverse @connectable_points if $direction == 1;
                Slic3r::debugf "  found %d connectable points = %s\n", scalar(@connectable_points),
                    join ', ', @connectable_points if $Slic3r::debug;
                
                if (!@connectable_points && @path_points && $path_points[-1][0] != $c) {
                    # no connectable in this row
                    $stop_path->();
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
        if ($axes[0] == 1) {
            @paths = map $self->_mgp_from_points_ref($_)->move(-$print->x_length, -$print->y_length)->rotate(-PI()/2)->points, @paths;
        }
        
        # save into layer
        push @{ $layer->fills }, map Slic3r::ExtrusionPath->new_from_points(@$_), @paths;
    }
}

# this function will select the first contiguous block of 
# points connectable to a given one
sub find_connectable_points {
    my $self = shift;
    my ($polygon, $point, $c, $points) = @_;
    
    my @connectable_points = ();
    foreach my $p (@$points) {
        push @connectable_points, $p 
            if $self->can_connect($polygon, $point, [ $c, $p ]);
    }
    return @connectable_points;
}

# this subroutine tries to determine whether two points in a surface
# are connectable without crossing contour or holes
sub can_connect {
    my $self = shift;
    my ($polygon, $p1, $p2) = @_;
    
    # there's room for optimization here
    
    # this is not needed since we assume that $p1 and $p2 belong to $polygon
    ###for ($p1, $p2) {
        ###return 0 unless $polygon->isinside($_);
    ###}
    
    # check whether the $p1-$p2 segment doesn't intersect any segment
    # of the contour or of holes
    foreach my $points (@{ $polygon->polygons }) {
        foreach my $line ($self->_lines_from_mgp_points($points)) {
            my $point = SegmentIntersection([$p1, $p2, @$line]);
            if ($point && !$self->points_coincide($point, $p1) && !$self->points_coincide($point, $p2)) {
                return 0;
            }
        }
    }
    
    return 1;
}

sub points_coincide {
    my $self = shift;
    my ($p1, $p2) = @_;
    return 0 if $p2->[0] - $p1->[0] < epsilon && $p2->[1] - $p1->[1] < epsilon;
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
