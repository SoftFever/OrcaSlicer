package Slic3r::Perimeter;
use Moo;

use Math::Clipper ':all';
use Math::ConvexHull qw(convex_hull);
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
    
    my (%contours, %holes) = ();
    foreach my $surface (@{ $layer->perimeter_surfaces }) {
        $contours{$surface} = [];
        $holes{$surface} = [];
        my @last_offsets = ();
        
        # first perimeter
        {
            my $polygon = $surface->clipper_polygon;
            my ($contour_p, @holes_p) = ($polygon->{outer}, @{$polygon->{holes}});
            push @{ $contours{$surface} }, $contour_p;
            push @{ $holes{$surface} }, @holes_p;
            @last_offsets = ($polygon);
        }
        
        # create other offsets
        for (my $loop = 1; $loop < $Slic3r::perimeter_offsets; $loop++) {
            
            # offsetting a polygon can result in one or many offset polygons
            @last_offsets = map $self->offset_polygon($_), @last_offsets;
            
            foreach my $offset_polygon (@last_offsets) {
                my ($contour_p, @holes_p) = ($offset_polygon->{outer}, @{$offset_polygon->{holes}});
                
                push @{ $contours{$surface} }, $contour_p;
                push @{ $holes{$surface} }, @holes_p;
            }
        }
        
        # create one more offset to be used as boundary for fill
        {
            my @fill_surfaces = map Slic3r::Surface->cast_from_expolygon(
                $_,
                surface_type => $surface->surface_type,
            ), map $self->offset_polygon($_), @last_offsets;
            
            push @{ $layer->fill_surfaces }, Slic3r::Surface::Collection->new(
                surfaces => [@fill_surfaces],
            ) if @fill_surfaces;
        }
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
    if ($layer->id == 0 && $Slic3r::skirts > 0 && @{ $layer->surfaces }) {
        # find out convex hull
        my $convex_hull = convex_hull([ map @$_, map $_->p, @{ $layer->surfaces } ]);
        
        # draw outlines from outside to inside
        for (my $i = $Slic3r::skirts - 1; $i >= 0; $i--) {
            my $distance = ($Slic3r::skirt_distance + ($Slic3r::flow_width * $i)) / $Slic3r::resolution;
            my $outline = offset([$convex_hull], $distance, $Slic3r::resolution * 100, JT_ROUND);
            push @{ $layer->skirts }, Slic3r::ExtrusionLoop->cast([ @{$outline->[0]} ]);
        }
    }
}

sub offset_polygon {
    my $self = shift;
    my ($polygon) = @_;
    # $polygon holds a Math::Clipper ExPolygon hashref representing 
    # a polygon and its holes
    
    # generate offsets
    my $distance = $Slic3r::flow_width / $Slic3r::resolution;
    my $offsets = offset([ $polygon->{outer}, @{$polygon->{holes}} ], -$distance, 
        $Slic3r::resolution * 100000, JT_MITER, 2);
    
    # defensive programming
    my (@contour_offsets, @hole_offsets) = ();
    for (@$offsets) {
        if (is_counter_clockwise($_)) {
            push @contour_offsets, $_;
        } else {
            push @hole_offsets, $_;
        }
    }
    
    # apply holes to the right contours
    my $clipper = Math::Clipper->new;
    $clipper->add_subject_polygons($offsets);
    my $results = $clipper->ex_execute(CT_UNION, PFT_NONZERO, PFT_NONZERO);
    return @$results;
}

1;
