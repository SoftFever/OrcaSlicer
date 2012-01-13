package Slic3r::Perimeter;
use Moo;

use Math::Clipper ':all';
use Slic3r::Geometry qw(X Y shortest_path scale);
use Slic3r::Geometry::Clipper qw(diff_ex);
use XXX;

sub make_perimeter {
    my $self = shift;
    my ($layer) = @_;
    printf "Making perimeter for layer %d\n", $layer->id;
    
    # at least one perimeter is required
    die "Can't slice object with no perimeters!\n"
        if $Slic3r::perimeters == 0;
    
    # this array will hold one arrayref per original surface;
    # each item of this arrayref is an arrayref representing a depth (from inner
    # perimeters to outer); each item of this arrayref is an ExPolygon:
    # @perimeters = (
    #    [ # first object (identified by a single surface before offsetting)
    #        [ Slic3r::ExPolygon, Slic3r::ExPolygon... ],  #depth 0: outer loop
    #        [ Slic3r::ExPolygon, Slic3r::ExPolygon... ],  #depth 1: inner loop
    #    ],
    #    [ # second object
    #        ...
    #    ]
    # )
    my @perimeters = ();  # one item per depth; each item
    
    # organize perimeter surfaces using a shortest path search
    my @surfaces = @{shortest_path([
        map [ $_->contour->[0], $_ ], @{$layer->slices},
    ])};
    
    foreach my $surface (@surfaces) {
        my @last_offsets = ($surface->expolygon);
        my $distance = 0;
        
        # create other offsets
        push @perimeters, [];
        for (my $loop = 0; $loop < $Slic3r::perimeters; $loop++) {
            # offsetting a polygon can result in one or many offset polygons
            @last_offsets = map $_->offset_ex(-$distance), @last_offsets if $distance;
            last if !@last_offsets;
            push @{ $perimeters[-1] }, [@last_offsets];
            
            # offset distance for inner loops
            $distance = scale $Slic3r::flow_spacing;
        }
        
        # create one more offset to be used as boundary for fill
        {
            my @fill_boundaries = map $_->offset_ex(-$distance), @last_offsets;
            push @{ $layer->fill_boundaries }, @fill_boundaries;
            
            # detect the small gaps that we need to treat like thin polygons,
            # thus generating the skeleton and using it to fill them
            my $small_gaps = diff_ex(
                [ map @$_, map $_->offset_ex(-$distance/2), map @$_, @{$perimeters[-1]} ],
                [ map @$_, map $_->offset_ex(+$distance/2), @fill_boundaries ],
            );
            push @{ $layer->thin_fills },
                grep $_,
                map $_->medial_axis(scale $Slic3r::flow_width),
                @$small_gaps if 0;
        }
    }
    
    # process one island (original surface) at time
    foreach my $island (@perimeters) {
        # do holes starting from innermost one
        foreach my $hole (map $_->holes, map @$_, @$island) {
            push @{ $layer->perimeters }, Slic3r::ExtrusionLoop->new(polygon => $hole, role => 'perimeter');
        }
        
        # do contours starting from innermost one
        foreach my $contour (map $_->contour, map @$_, reverse @$island) {
            push @{ $layer->perimeters }, Slic3r::ExtrusionLoop->new(polygon => $contour, role => 'perimeter');
        }
    }
    
    # detect small perimeters by checking their area
    for (@{ $layer->perimeters }) {
        $_->role('small-perimeter') if $_->polygon->area < $Slic3r::small_perimeter_area;
    }
    
    # add thin walls as perimeters
    for (@{ $layer->thin_walls }) {
        if ($_->isa('Slic3r::Polygon')) {
            push @{ $layer->perimeters }, Slic3r::ExtrusionLoop->new(polygon => $_, role => 'perimeter');
        } else {
            push @{ $layer->perimeters }, Slic3r::ExtrusionPath->new(polyline => $_, role => 'perimeter');
        }
    }
}

1;
