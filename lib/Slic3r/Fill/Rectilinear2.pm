package Slic3r::Fill::Rectilinear2;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(scale X1 Y1 X2 Y2 A B X Y);
use XXX;

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    my $polygons = [ $surface->p ];
    
    # rotate polygons so that we can work with vertical lines here
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($polygons, $rotate_vector);
    
    my $distance_between_lines = scale $params{flow_spacing} / $params{density};
    my $number_of_lines = int(0.99999999 + $self->max_print_dimension / $distance_between_lines); # ceil

    #printf "distance = %f\n", $distance_between_lines;
    #printf "number_of_lines = %d\n", $number_of_lines;
    
    # this arrayref will hold intersection points of the fill grid with surface segments
    my $points = [ map [], 0..$number_of_lines-1 ];
    foreach my $line (map Slic3r::Geometry::polygon_lines($_), @$polygons) {
        
        # find out the coordinates
        my @coordinates = map @$_, @$line;
        
        # get the extents of the segment along the primary axis
        my @line_c = sort { $a <=> $b } @coordinates[X1, X2];
        Slic3r::debugf "Segment %d,%d - %d,%d (extents: %f, %f)\n", @coordinates, @line_c;
        
        for (my $c = int($line_c[0] / $distance_between_lines) * $distance_between_lines; 
                $c <= $line_c[1]; $c += $distance_between_lines) {
            next if $c < $line_c[0] || $c > $line_c[1];
            my $i = sprintf('%.0f', $c / $distance_between_lines) - 1;
            #printf "CURRENT \$i = %d, \$c = %f\n", $i, $c;
            
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
            my @connectable_points = $self->find_connectable_points($polygons, $path_points[-1], $c, [@search_points]);
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
    $self->rotate_points_back(\@paths, $rotate_vector);
    
    return @paths;
}

# this function will select the first contiguous block of 
# points connectable to a given one
sub find_connectable_points {
    my $self = shift;
    my ($polygons, $point, $c, $points) = @_;
    
    my @connectable_points = ();
    foreach my $p (@$points) {
        if (!Slic3r::Geometry::can_connect_points($point, [ $c, $p ], $polygons)) {
             @connectable_points ? last : next;
        }
        push @connectable_points, $p;
        $point = [ $c, $p ] if $point->[0] != $c;
    }
    return @connectable_points;
}

1;
