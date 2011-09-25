package Slic3r::Perimeter;
use Moo;

use Math::Clipper ':all';
use Math::Geometry::Planar;
*Math::Geometry::Planar::OffsetPolygon = *Math::Geometry::Planar::Offset::OffsetPolygon;
use XXX;

use constant X => 0;
use constant Y => 1;

sub make_perimeter {
    my $self = shift;
    my ($layer) = @_;
    printf "Making perimeter for layer %d:\n", $layer->id;
    
    # at least one perimeter is required
    die "Can't extrude object without any perimeter!\n"
        if $Slic3r::perimeter_offsets == 0;
    
    my (@perimeters, %contours, %holes) = ();
    foreach my $surface (@{ $layer->surfaces }) {
        $contours{$surface} = [];
        $holes{$surface} = [];
        
        # first perimeter
        {
            my $polygon = $surface->clipper_polygon;
            my ($contour_p, @holes_p) = ($polygon->{outer}, @{$polygon->{holes}});
            push @{ $contours{$surface} }, $contour_p;
            push @{ $holes{$surface} }, @holes_p;
            push @perimeters, $polygon;
        }
        
        # create other offsets
        for (my $loop = 1; $loop < $Slic3r::perimeter_offsets; $loop++) {
            
            # offsetting a polygon can result in one or many offset polygons
            my @offsets = $self->offset_polygon($perimeters[-1]);
            
            foreach my $offset_polygon (@offsets) {
                my ($contour_p, @holes_p) = ($offset_polygon->{outer}, @{$offset_polygon->{holes}});
                
                push @{ $contours{$surface} }, $contour_p;
                push @{ $holes{$surface} }, @holes_p;
                push @perimeters, $offset_polygon;
            }
        }
        
        # create one more offset to be used as boundary for fill
        push @{ $layer->fill_surfaces },
            map Slic3r::Surface->new(
                surface_type => $surface->surface_type,
                contour      => Slic3r::Polyline::Closed->cast($_->{outer}),
                holes        => [
                    map Slic3r::Polyline::Closed->cast($_), @{$_->{holes}}
                ],
            ), $self->offset_polygon($perimeters[-1]),
    }
    
    # generate paths for holes
    # we start from innermost loops (that is, external ones), do them
    # for all holes, than go on with inner loop and do that for all
    # holes and so on
    foreach my $hole (map @$_, values %holes) {
        my @points = @$hole;
        push @points, [ @{$points[0]} ];
        # to avoid blobs, the first point is replaced by the point of
        # the segment which is $Slic3r::flow_width / $Slic3r::resolution 
        # away from it to avoid the extruder to get two times there
        $points[0] = $self->_get_point_along_line($points[0], $points[1], 
            $Slic3r::flow_width / $Slic3r::resolution);
        push @{ $layer->perimeters }, Slic3r::ExtrusionPath->cast([@points]);
    }
    
    # generate paths for contours
    # this time we do something different: we do contour loops for one
    # shape (that is, one original surface) at a time: we start from the
    # innermost loop (that is, internal one), then without interrupting 
    # our path we go onto the outer loop and continue; this should ensure
    # good surface quality
    foreach my $polylines (values %contours) {
        my @path_points = ();
        foreach my $p (map $self->_mgp_from_points_ref($_), @$polylines) {
            my $points = $p->points;
            # to avoid blobs, the first point is replaced by the point of
            # the segment which is $Slic3r::flow_width / $Slic3r::resolution 
            # away from it to avoid the extruder to get two times there
            push @$points, [ @{$points->[0]} ];
            $points->[0] = $self->_get_point_along_line($points->[0], $points->[1], 
                $Slic3r::flow_width / $Slic3r::resolution);
            push @path_points, @$points;
        }
        push @{ $layer->perimeters }, Slic3r::ExtrusionPath->cast([ reverse @path_points ]);
    }
    
    # generate skirt on bottom layer
    if ($layer->id == 0 && $Slic3r::skirts > 0) {
        # find out convex hull
        my $points = [ map { @{ $_->mgp_polygon->polygons->[0] } } @{ $layer->surfaces } ];
        my $convex_hull = $self->_mgp_from_points_ref($points)->convexhull2;
        my $convex_hull_polygon = ref $convex_hull eq 'ARRAY' 
            ? $self->_mgp_from_points_ref($convex_hull)
            : $convex_hull;
        
        # draw outlines from outside to inside
        for (my $i = $Slic3r::skirts - 1; $i >= 0; $i--) {
            my $outline = $convex_hull_polygon->offset_polygon(
                - ($Slic3r::skirt_distance + ($Slic3r::flow_width * $i)) / $Slic3r::resolution
            );
            push @{$outline->[0]}, $outline->[0][0]; # repeat first point as last to complete the loop
            push @{ $layer->skirts }, Slic3r::ExtrusionPath->cast([ @{$outline->[0]} ]);
        }
    }
}

sub offset_polygon {
    my $self = shift;
    my ($polygon) = @_;
    
    my $distance = $Slic3r::flow_width / $Slic3r::resolution;
    
    # $polygon holds a Math::Clipper ExPolygon hashref representing 
    # a polygon and its holes
    my ($contour_p, @holes_p) = ($polygon->{outer}, @{$polygon->{holes}});
    
    # generate offsets
    my $offsets = offset([ $contour_p, @holes_p ], -$distance, 1);
    
    # fix order of holes
    @$offsets = map [ reverse @$_ ], @$offsets;
    
    # defensive programming
    my (@contour_offsets, @hole_offsets) = ();
    for (@$offsets) {
        if (is_counter_clockwise($_)) {
            push @contour_offsets, $_;
        } else {
            push @hole_offsets, $_;
        }
    }
    
    # apply all holes to all contours;
    # this is improper, but Math::Clipper handles it
    return map {{
        outer => $_,
        holes => [ @hole_offsets ],
    }} @contour_offsets;
}

sub _mgp_from_points_ref {
    my $self = shift;
    my ($points) = @_;
    my $p = Math::Geometry::Planar->new;
    $p->points($points);
    return $p;
}

sub _mgp_from_polygons_ref {
    my $self = shift;
    my ($polygons) = @_;
    my $p = Math::Geometry::Planar->new;
    $p->polygons($polygons);
    return $p;
}

sub _get_point_along_line {
    my $self = shift;
    my ($p1, $p2, $distance) = @_;
    
    my $point = [ @$p1 ];
    
    my $line_length = sqrt( (($p2->[X] - $p1->[X])**2) + (($p2->[Y] - $p1->[Y])**2) );
    for (X, Y) {
        if ($p1->[$_] != $p2->[$_]) {
            $point->[$_] = $p1->[$_] + ($p2->[$_] - $p1->[$_]) * $distance / $line_length;
        }
    }
    
    return $point;
}

1;
