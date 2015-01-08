package Slic3r::Layer::PerimeterGenerator;
use Moo;

use Slic3r::ExtrusionLoop ':roles';
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scale unscale chained_path);
use Slic3r::Geometry::Clipper qw(union_ex diff diff_ex intersection_ex offset offset2
    offset_ex offset2_ex union_pt intersection_ppl diff_ppl);
use Slic3r::Surface ':types';

has 'slices'                => (is => 'ro', required => 1);  # SurfaceCollection
has 'lower_slices'          => (is => 'ro', required => 0);
has 'layer_height'          => (is => 'ro', required => 1);
has 'layer_id'              => (is => 'ro', required => 0, default => sub { -1 });
has 'perimeter_flow'        => (is => 'ro', required => 1);
has 'ext_perimeter_flow'    => (is => 'ro', required => 1);
has 'overhang_flow'         => (is => 'ro', required => 1);
has 'solid_infill_flow'     => (is => 'ro', required => 1);
has 'config'                => (is => 'ro', default => sub { Slic3r::Config::PrintRegion->new });
has 'print_config'          => (is => 'ro', default => sub { Slic3r::Config::Print->new });
has '_lower_slices_p'       => (is => 'rw', default => sub { [] });
has '_holes_pt'             => (is => 'rw');
has '_ext_mm3_per_mm'       => (is => 'rw');
has '_mm3_per_mm'           => (is => 'rw');
has '_mm3_per_mm_overhang'  => (is => 'rw');
has '_thin_wall_polylines'  => (is => 'rw', default => sub { [] });

# generated loops will be put here
has 'loops'         => (is => 'ro', default => sub { Slic3r::ExtrusionPath::Collection->new });

# generated gap fills will be put here
has 'gap_fill'      => (is => 'ro', default => sub { Slic3r::ExtrusionPath::Collection->new });

# generated fill surfaces will be put here
has 'fill_surfaces' => (is => 'ro', default => sub { Slic3r::Surface::Collection->new });

sub BUILDARGS {
    my ($class, %args) = @_;
    
    if (my $flow = delete $args{flow}) {
        $args{perimeter_flow}       //= $flow;
        $args{ext_perimeter_flow}   //= $flow;
        $args{overhang_flow}        //= $flow;
        $args{solid_infill_flow}    //= $flow;
    }
    
    return { %args };
}

sub process {
    my ($self) = @_;
    
    # other perimeters
    $self->_mm3_per_mm($self->perimeter_flow->mm3_per_mm);
    my $pwidth              = $self->perimeter_flow->scaled_width;
    my $pspacing            = $self->perimeter_flow->scaled_spacing;
    
    # external perimeters
    $self->_ext_mm3_per_mm($self->ext_perimeter_flow->mm3_per_mm);
    my $ext_pwidth          = $self->ext_perimeter_flow->scaled_width;
    my $ext_pspacing        = scale($self->ext_perimeter_flow->spacing_to($self->perimeter_flow));
    
    # overhang perimeters
    $self->_mm3_per_mm_overhang($self->overhang_flow->mm3_per_mm);
    
    # solid infill
    my $ispacing            = $self->solid_infill_flow->scaled_spacing;
    my $gap_area_threshold  = $pwidth ** 2;
    
    # Calculate the minimum required spacing between two adjacent traces.
    # This should be equal to the nominal flow spacing but we experiment
    # with some tolerance in order to avoid triggering medial axis when
    # some squishing might work. Loops are still spaced by the entire
    # flow spacing; this only applies to collapsing parts.
    my $min_spacing         = $pspacing * (1 - &Slic3r::INSET_OVERLAP_TOLERANCE);
    my $ext_min_spacing     = $ext_pspacing * (1 - &Slic3r::INSET_OVERLAP_TOLERANCE);
    
    # prepare grown lower layer slices for overhang detection
    if ($self->lower_slices && $self->config->overhangs) {
        # We consider overhang any part where the entire nozzle diameter is not supported by the
        # lower layer, so we take lower slices and offset them by half the nozzle diameter used 
        # in the current layer
        my $nozzle_diameter = $self->print_config->get_at('nozzle_diameter', $self->config->perimeter_extruder-1);
        
        $self->_lower_slices_p(
            offset([ map @$_, @{$self->lower_slices} ], scale +$nozzle_diameter/2)
        );
    }
    
    # we need to process each island separately because we might have different
    # extra perimeters for each one
    foreach my $surface (@{$self->slices}) {
        my @contours    = ();    # array of Polygons with ccw orientation
        my @holes       = ();    # array of Polygons with cw orientation
        my @thin_walls  = ();    # array of ExPolygons
        
        # detect how many perimeters must be generated for this island
        my $loop_number = $self->config->perimeters + ($surface->extra_perimeters || 0);
        
        my @last = @{$surface->expolygon};
        my @gaps = ();    # array of ExPolygons
        if ($loop_number > 0) {
            # we loop one time more than needed in order to find gaps after the last perimeter was applied
            for my $i (1 .. ($loop_number+1)) {  # outer loop is 1
                my @offsets = ();
                if ($i == 1) {
                    # the minimum thickness of a single loop is:
                    # ext_width/2 + ext_spacing/2 + spacing/2 + width/2
                    if ($self->config->thin_walls) {
                        @offsets = @{offset2(
                            \@last,
                            -(0.5*$ext_pwidth + 0.5*$ext_min_spacing - 1),
                            +(0.5*$ext_min_spacing - 1),
                        )};
                    } else {
                        @offsets = @{offset(
                            \@last,
                            -0.5*$ext_pwidth,
                        )};
                    }
                    
                    # look for thin walls
                    if ($self->config->thin_walls) {
                        my $diff = diff_ex(
                            \@last,
                            offset(\@offsets, +0.5*$ext_pwidth),
                            1,  # medial axis requires non-overlapping geometry
                        );
                        push @thin_walls, @$diff;
                    }
                } else {
                    my $distance = ($i == 2) ? $ext_pspacing : $pspacing;
                    
                    if ($self->config->thin_walls) {
                        @offsets = @{offset2(
                            \@last,
                            -($distance + 0.5*$min_spacing - 1),
                            +(0.5*$min_spacing - 1),
                        )};
                    } else {
                        @offsets = @{offset(
                            \@last,
                            -$distance,
                        )};
                    }
                    
                    # look for gaps
                    if ($self->config->gap_fill_speed > 0 && $self->config->fill_density > 0) {
                        # not using safety offset here would "detect" very narrow gaps
                        # (but still long enough to escape the area threshold) that gap fill
                        # won't be able to fill but we'd still remove from infill area
                        my $diff = diff_ex(
                            offset(\@last, -0.5*$pspacing),
                            offset(\@offsets, +0.5*$pspacing + 10),  # safety offset
                        );
                        push @gaps, grep abs($_->area) >= $gap_area_threshold, @$diff;
                    }
                }
            
                last if !@offsets;
                last if $i > $loop_number; # we were only looking for gaps this time
            
                # clone polygons because these ExPolygons will go out of scope very soon
                @last = @offsets;
                foreach my $polygon (@offsets) {
                    if ($polygon->is_counter_clockwise) {
                        push @contours, $polygon;
                    } else {
                        push @holes, $polygon;
                    }
                }
            }
        }
        
        # fill gaps
        if (@gaps) {
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output(
                    "gaps.svg",
                    expolygons => \@gaps,
                );
            }
            
            # where $pwidth < thickness < 2*$pspacing, infill with width = 1.5*$pwidth
            # where 0.5*$pwidth < thickness < $pwidth, infill with width = 0.5*$pwidth
            my @gap_sizes = (
                [ $pwidth, 2*$pspacing, unscale 1.5*$pwidth ],
                [ 0.5*$pwidth, $pwidth, unscale 0.5*$pwidth ],
            );
            foreach my $gap_size (@gap_sizes) {
                my @gap_fill = $self->_fill_gaps(@$gap_size, \@gaps);
                $self->gap_fill->append($_) for @gap_fill;
            
                # Make sure we don't infill narrow parts that are already gap-filled
                # (we only consider this surface's gaps to reduce the diff() complexity).
                # Growing actual extrusions ensures that gaps not filled by medial axis
                # are not subtracted from fill surfaces (they might be too short gaps
                # that medial axis skips but infill might join with other infill regions
                # and use zigzag).
                my $w = $gap_size->[2];
                my @filled = map {
                    @{($_->isa('Slic3r::ExtrusionLoop') ? $_->polygon->split_at_first_point : $_->polyline)
                        ->grow(scale $w/2)};
                } @gap_fill;
                @last = @{diff(\@last, \@filled)};
            }
        }
        
        # create one more offset to be used as boundary for fill
        # we offset by half the perimeter spacing (to get to the actual infill boundary)
        # and then we offset back and forth by half the infill spacing to only consider the
        # non-collapsing regions
        my $min_perimeter_infill_spacing = $ispacing * (1 - &Slic3r::INSET_OVERLAP_TOLERANCE);
        $self->fill_surfaces->append($_)
            for map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNAL),  # use a bogus surface type
                @{offset2_ex(
                    [ map @{$_->simplify_p(&Slic3r::SCALED_RESOLUTION)}, @{union_ex(\@last)} ],
                    -($pspacing/2 + $min_perimeter_infill_spacing/2),
                    +$min_perimeter_infill_spacing/2,
                )};
    
    
        # process thin walls by collapsing slices to single passes
        if (@thin_walls) {
            # the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
            # (actually, something larger than that still may exist due to mitering or other causes)
            my $min_width = $pwidth / 4;
            @thin_walls = @{offset2_ex([ map @$_, @thin_walls ], -$min_width/2, +$min_width/2)};
        
            # the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
            $self->_thin_wall_polylines([ map @{$_->medial_axis($pwidth + $pspacing, $min_width)}, @thin_walls ]);
            Slic3r::debugf "  %d thin walls detected\n", scalar(@{$self->_thin_wall_polylines}) if $Slic3r::debug;
        
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output(
                    "medial_axis.svg",
                    no_arrows => 1,
                    expolygons      => \@thin_walls,
                    green_polylines => [ map $_->polygon->split_at_first_point, @{$self->perimeters} ],
                    red_polylines   => $self->_thin_wall_polylines,
                );
            }
        }
        
        # find nesting hierarchies separately for contours and holes
        my $contours_pt = union_pt(\@contours);
        $self->_holes_pt(union_pt(\@holes));
    
        # order loops from inner to outer (in terms of object slices)
        my @loops = $self->_traverse_pt($contours_pt, 0, 1);
    
        # if brim will be printed, reverse the order of perimeters so that
        # we continue inwards after having finished the brim
        # TODO: add test for perimeter order
        @loops = reverse @loops
            if $self->config->external_perimeters_first
                || ($self->layer_id == 0 && $self->print_config->brim_width > 0);
        
        # append perimeters for this slice as a collection
        $self->loops->append(Slic3r::ExtrusionPath::Collection->new(@loops));
    }
}

sub _traverse_pt {
    my ($self, $polynodes, $depth, $is_contour) = @_;
    
    # convert all polynodes to ExtrusionLoop objects
    my $collection = Slic3r::ExtrusionPath::Collection->new;  # temporary collection
    my @children = ();
    foreach my $polynode (@$polynodes) {
        my $polygon = ($polynode->{outer} // $polynode->{hole})->clone;
        
        my $role        = EXTR_ROLE_PERIMETER;
        my $loop_role   = EXTRL_ROLE_DEFAULT;
        
        my $root_level  = $depth == 0;
        my $no_children = !@{ $polynode->{children} };
        my $is_external = $is_contour ? $root_level : $no_children;
        my $is_internal = $is_contour ? $no_children : $root_level;
        if ($is_contour && $is_internal) {
            # internal perimeters are root level in case of holes
            # and items with no children in case of contours
            # Note that we set loop role to ContourInternalPerimeter
            # also when loop is both internal and external (i.e.
            # there's only one contour loop).
            $loop_role  = EXTRL_ROLE_CONTOUR_INTERNAL_PERIMETER;
        }
        if ($is_external) {
            # external perimeters are root level in case of contours
            # and items with no children in case of holes
            $role       = EXTR_ROLE_EXTERNAL_PERIMETER;
        }
        
        # detect overhanging/bridging perimeters
        my @paths = ();
        if ($self->config->overhangs && $self->layer_id > 0) {
            # get non-overhang paths by intersecting this loop with the grown lower slices
            foreach my $polyline (@{ intersection_ppl([ $polygon ], $self->_lower_slices_p) }) {
                push @paths, Slic3r::ExtrusionPath->new(
                    polyline        => $polyline,
                    role            => $role,
                    mm3_per_mm      => ($is_external ? $self->_ext_mm3_per_mm : $self->_mm3_per_mm),
                    width           => ($is_external ? $self->ext_perimeter_flow->width : $self->perimeter_flow->width),
                    height          => $self->layer_height,
                );
            }
            
            # get overhang paths by checking what parts of this loop fall 
            # outside the grown lower slices (thus where the distance between
            # the loop centerline and original lower slices is >= half nozzle diameter
            foreach my $polyline (@{ diff_ppl([ $polygon ], $self->_lower_slices_p) }) {
                push @paths, Slic3r::ExtrusionPath->new(
                    polyline        => $polyline,
                    role            => EXTR_ROLE_OVERHANG_PERIMETER,
                    mm3_per_mm      => $self->_mm3_per_mm_overhang,
                    width           => $self->overhang_flow->width,
                    height          => $self->layer_height,
                );
            }
            
            # reapply the nearest point search for starting point
            # (clone because the collection gets DESTROY'ed)
            # We allow polyline reversal because Clipper may have randomly
            # reversed polylines during clipping.
            my $collection = Slic3r::ExtrusionPath::Collection->new(@paths); # temporary collection
            @paths = map $_->clone, @{$collection->chained_path(0)};
        } else {
            push @paths, Slic3r::ExtrusionPath->new(
                polyline        => $polygon->split_at_first_point,
                role            => $role,
                mm3_per_mm      => $self->_mm3_per_mm,
                width           => $self->perimeter_flow->width,
                height          => $self->layer_height,
            );
        }
        my $loop = Slic3r::ExtrusionLoop->new_from_paths(@paths);
        $loop->role($loop_role);
        
        # return ccw contours and cw holes
        # GCode.pm will convert all of them to ccw, but it needs to know
        # what the holes are in order to compute the correct inwards move
        # We do this on the final Loop object because overhang clipping
        # does not keep orientation.
        if ($is_contour) {
            $loop->make_counter_clockwise;
        } else {
            $loop->make_clockwise;
        }
        $collection->append($loop);
        
        # save the children
        push @children, $polynode->{children};
    }

    # if we're handling the top-level contours, add thin walls as candidates too
    # in order to include them in the nearest-neighbor search
    if ($is_contour && $depth == 0) {
        foreach my $polyline (@{$self->_thin_wall_polylines}) {
            $collection->append(Slic3r::ExtrusionPath->new(
                polyline        => $polyline,
                role            => EXTR_ROLE_EXTERNAL_PERIMETER,
                mm3_per_mm      => $self->_mm3_per_mm,
                width           => $self->perimeter_flow->width,
                height          => $self->layer_height,
            ));
        }
    }
    
    # use a nearest neighbor search to order these children
    # TODO: supply second argument to chained_path() too?
    # (We used to skip this chained_path() when $is_contour &&
    # $depth == 0 because slices are ordered at G_code export 
    # time, but multiple top-level perimeters might belong to
    # the same slice actually, so that was a broken optimization.)
    # We supply no_reverse = false because we want to permit reversal
    # of thin walls, but we rely on the fact that loops will never
    # be reversed anyway.
    my $sorted_collection = $collection->chained_path_indices(0);
    my @orig_indices = @{$sorted_collection->orig_indices};
    
    my @loops = ();
    foreach my $loop (@$sorted_collection) {
        my $orig_index = shift @orig_indices;
        
        if ($loop->isa('Slic3r::ExtrusionPath')) {
            push @loops, $loop->clone;
        } else {
            # if this is an external contour find all holes belonging to this contour(s)
            # and prepend them
            if ($is_contour && $depth == 0) {
                # $loop is the outermost loop of an island
                my @holes = ();
                for (my $i = 0; $i <= $#{$self->_holes_pt}; $i++) {
                    if ($loop->polygon->contains_point($self->_holes_pt->[$i]{outer}->first_point)) {
                        push @holes, splice @{$self->_holes_pt}, $i, 1;  # remove from candidates to reduce complexity
                        $i--;
                    }
                }
                
                # order holes efficiently
                @holes = @holes[@{chained_path([ map {($_->{outer} // $_->{hole})->first_point} @holes ])}];
                
                push @loops, reverse map $self->_traverse_pt([$_], 0, 0), @holes;
            }
            
            # traverse children and prepend them to this loop
            push @loops, $self->_traverse_pt($children[$orig_index], $depth+1, $is_contour);
            push @loops, $loop->clone;
        }
    }
    return @loops;
}

sub _fill_gaps {
    my ($self, $min, $max, $w, $gaps) = @_;
    
    my $this = diff_ex(
        offset2([ map @$_, @$gaps ], -$min/2, +$min/2),
        offset2([ map @$_, @$gaps ], -$max/2, +$max/2),
        1,
    );
    my @polylines = map @{$_->medial_axis($max, $min/2)}, @$this;
    return if !@polylines;
    
    Slic3r::debugf "  %d gaps filled with extrusion width = %s\n", scalar @$this, $w
        if @$this;

    #my $flow = $layerm->flow(FLOW_ROLE_SOLID_INFILL, 0, $w);
    my $flow = Slic3r::Flow->new(
        width           => $w,
        height          => $self->layer_height,
        nozzle_diameter => $self->solid_infill_flow->nozzle_diameter,
    );
    
    my %path_args = (
        role        => EXTR_ROLE_GAPFILL,
        mm3_per_mm  => $flow->mm3_per_mm,
        width       => $flow->width,
        height      => $self->layer_height,
    );
    
    my @entities = ();
    foreach my $polyline (@polylines) {
        #if ($polylines[$i]->isa('Slic3r::Polygon')) {
        #    my $loop = Slic3r::ExtrusionLoop->new;
        #    $loop->append(Slic3r::ExtrusionPath->new(polyline => $polylines[$i]->split_at_first_point, %path_args));
        #    $polylines[$i] = $loop;
        if ($polyline->is_valid && $polyline->first_point->coincides_with($polyline->last_point)) {
            # since medial_axis() now returns only Polyline objects, detect loops here
            push @entities, my $loop = Slic3r::ExtrusionLoop->new;
            $loop->append(Slic3r::ExtrusionPath->new(polyline => $polyline, %path_args));
        } else {
            push @entities, Slic3r::ExtrusionPath->new(polyline => $polyline, %path_args);
        }
    }
    
    return @entities;
}

1;
