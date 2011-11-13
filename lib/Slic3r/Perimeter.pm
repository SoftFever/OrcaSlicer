package Slic3r::Perimeter;
use Moo;

use Math::Clipper ':all';
use Slic3r::Geometry qw(X Y shortest_path);
use XXX;

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
    #        [ Slic3r::ExPolygon, Slic3r::ExPolygon... ],  #depth 0: outer loop
    #        [ Slic3r::ExPolygon, Slic3r::ExPolygon... ],  #depth 1: inner loop
    #    ],
    #    [ # second object
    #        ...
    #    ]
    # )
    my @perimeters = ();  # one item per depth; each item
    
    # organize $layer->perimeter_surfaces using a shortest path search
    @{ $layer->perimeter_surfaces } = @{shortest_path([
        map [ $_->contour->points->[0], $_ ], @{ $layer->perimeter_surfaces },
    ])};
    
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
            $distance -= $Slic3r::flow_width * $Slic3r::perimeter_infill_overlap_ratio / $Slic3r::resolution;
            my @fill_surfaces = map Slic3r::Surface->cast_from_expolygon
                ($_, surface_type => $surface->surface_type),
                map $_->offset(-$distance), @last_offsets;
            
            push @{ $layer->fill_surfaces }, [@fill_surfaces] if @fill_surfaces;
        }
    }
    
    # process one island (original surface) at time
    foreach my $island (@perimeters) {
        # do holes starting from innermost one
        foreach my $hole (map $_->holes, map @$_, @$island) {
            push @{ $layer->perimeters }, Slic3r::ExtrusionLoop->cast($hole);
        }
        
        # do contours starting from innermost one
        foreach my $contour (map $_->contour, map @$_, reverse @$island) {
            push @{ $layer->perimeters }, Slic3r::ExtrusionLoop->cast($contour);
        }
    }
}

1;
