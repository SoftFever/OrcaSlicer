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

use POSIX qw(ceil);
use XXX;

sub make_fill {
    my $self = shift;
    my ($print, $layer) = @_;
    printf "Filling layer %d:\n", $layer->id;
    
    my $max_print_dimension = $print->max_length * sqrt(2);
    
    my $n = 1;
    foreach my $surface_collection (@{ $layer->fill_surfaces }) {
        my @path_collection = ();
        
        SURFACE: foreach my $surface (@{ $surface_collection->surfaces }) {
            Slic3r::debugf " Processing surface %s:\n", $surface->id;
            my $polygons = [ $surface->p ];
            
            # set infill angle
            my (@rotate, @shift);
            $rotate[0] = Slic3r::Geometry::deg2rad($Slic3r::fill_angle);
            $rotate[1] = [ $max_print_dimension / 2, $max_print_dimension / 2 ];
            @shift = @{$rotate[1]};
            
            # alternate fill direction
            if ($layer->id % 2) {
                $rotate[0] = Slic3r::Geometry::deg2rad($Slic3r::fill_angle) + PI/2;
            }
            
            # TODO: here we should implement an "infill in direction of bridges" option
            
            # rotate surface as needed
            @shift = @{ +(Slic3r::Geometry::rotate_points(@rotate, \@shift))[0] };
            @$polygons = map [ Slic3r::Geometry::move_points(\@shift, @$_) ],
                map [ Slic3r::Geometry::rotate_points(@rotate, @$_) ], @$polygons if $rotate[0];
            
            # force 100% density for external surfaces
            my $density = $surface->surface_type eq 'internal' ? $Slic3r::fill_density : 1;
            next SURFACE unless $density > 0;
            
            my $distance_between_lines = $Slic3r::flow_width / $Slic3r::resolution / $density;
            my $number_of_lines = ceil($max_print_dimension / $distance_between_lines);
    
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
            if ($rotate[0]) {
                @paths = map [ Slic3r::Geometry::rotate_points(-$rotate[0], $rotate[1], @$_) ], 
                    map [ Slic3r::Geometry::move_points([map -$_, @shift], @$_) ], @paths;
            }
            
            push @path_collection, @paths;
        }
        
        # save into layer
        push @{ $layer->fills }, Slic3r::ExtrusionPath::Collection->new(
            paths => [ map Slic3r::ExtrusionPath->cast([ @$_ ]), @path_collection ],
        );
        $layer->fills->[-1]->cleanup;
    }
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
