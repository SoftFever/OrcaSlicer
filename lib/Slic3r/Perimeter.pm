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
        push @{ $layer->fill_surfaces }, Slic3r::Surface::Collection->new(
            surfaces => [
                map Slic3r::Surface->new(
                    surface_type => $surface->surface_type,
                    contour      => Slic3r::Polyline::Closed->cast($_->{outer}),
                    holes        => [
                        map Slic3r::Polyline::Closed->cast($_), @{$_->{holes}}
                    ],
                ), $self->offset_polygon($perimeters[-1]),
            ],
        );
    }
    
    # generate paths for holes:
    # we start from innermost loops (that is, external ones), do them
    # for all holes, than go on with inner loop and do that for all
    # holes and so on;
    # then we generate paths for contours:
    # this time we do something different: we do contour loops for one
    # shape (that is, one original surface) at a time: we start from the
    # innermost loop (that is, internal one), then without interrupting 
    # our path we go onto the outer loop and continue; this should ensure
    # good surface quality
    foreach my $p (map @$_, values %holes, values %contours) {
        push @{ $layer->perimeters }, Slic3r::ExtrusionLoop->cast($p);
    }
    
    # generate skirt on bottom layer
    if ($layer->id == 0 && $Slic3r::skirts > 0) {
        # find out convex hull
        my $points = [ map { @{ $_->mgp_polygon->polygons->[0] } } @{ $layer->surfaces } ];
        my $convex_hull = $self->_mgp_from_points_ref($points)->convexhull2;  # maybe Math::ConvexHull is faster?
        my $convex_hull_points = ref $convex_hull eq 'ARRAY' ? $convex_hull : $convex_hull->points;
        
        # draw outlines from outside to inside
        for (my $i = $Slic3r::skirts - 1; $i >= 0; $i--) {
            my $distance = ($Slic3r::skirt_distance + ($Slic3r::flow_width * $i)) / $Slic3r::resolution;
            my $outline = offset([$convex_hull_points], $distance, 0.1, JT_ROUND);
            push @{ $layer->skirts }, Slic3r::ExtrusionLoop->cast([ @{$outline->[0]} ]);
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
    my $offsets = offset([ $contour_p, @holes_p ], -$distance, 100, JT_MITER, 2);
    
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

1;
