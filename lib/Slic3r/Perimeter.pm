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
    die "Can't slice object with no perimeters!\n"
        if $Slic3r::perimeter_offsets == 0;
    
    # this array will hold one arrayref per original surface;
    # each item of this arrayref is an arrayref representing a depth (from inner
    # perimeters to outer); each item of this arrayref is an ExPolygon:
    # @perimeters = (
    #    [ # first object (identified by a single surface before offsetting)
    #        [ Slic3r::ExPolygon, Slic3r::ExPolygon... ],  #item 0: outer loop
    #        [ Slic3r::ExPolygon, Slic3r::ExPolygon... ],  #item 1: inner loop
    #    ],
    #    [ # second object
    #        ...
    #    ]
    # )
    my @perimeters = ();  # one item per depth; each item
    
    foreach my $surface (@{ $layer->perimeter_surfaces }) {
        # the outer loop must be offsetted by half extrusion width inwards
        my @last_offsets = ($surface->expolygon);
        my $distance = $Slic3r::flow_width / 2 / $Slic3r::resolution;
        
        # create other offsets
        push @perimeters, [];
        for (my $loop = 0; $loop < $Slic3r::perimeter_offsets; $loop++) {
            # offsetting a polygon can result in one or many offset polygons
            @last_offsets = map $_->offset(-$distance), @last_offsets;
            push @{ $perimeters[-1] }, [@last_offsets];
            
            # offset distance for inner loops
            $distance = $Slic3r::flow_width / $Slic3r::resolution;
        }
        
        # create one more offset to be used as boundary for fill
        {
            my @fill_surfaces = map Slic3r::Surface->cast_from_expolygon
                ($_, surface_type => $surface->surface_type),
                map $_->offset(-$distance), @last_offsets;
            
            push @{ $layer->fill_surfaces }, [@fill_surfaces] if @fill_surfaces;
        }
    }
    
    # first generate paths for all holes, starting from external (innermost) perimeters
    foreach my $i (1..$Slic3r::perimeter_offsets) {
        foreach my $hole (map $_->holes, map @{$_->[$i-1]}, @perimeters) {
            push @{ $layer->perimeters }, Slic3r::ExtrusionLoop->cast($hole);
        }
    }
    
    # then generate paths for contours
    # this time we do something different: we do contour loops for one
    # shape (that is, one original surface) at a time: we start from the
    # innermost loop (that is, internal one), then without interrupting 
    # our path we go onto the outer loop and continue; this should ensure
    # good surface quality
    foreach my $contour (map $_->contour, map @$_, map @$_, @perimeters) {
        push @{ $layer->perimeters }, Slic3r::ExtrusionLoop->cast($contour);
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

1;
