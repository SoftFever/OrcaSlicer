package Slic3r::Layer::PerimeterGenerator;
use Moo;

use Slic3r::ExtrusionLoop ':roles';
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scale unscale chained_path);
use Slic3r::Geometry::Clipper qw(union_ex diff diff_ex intersection_ex offset offset2
    offset_ex offset2_ex intersection_ppl diff_ppl);
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
has 'object_config'         => (is => 'ro', default => sub { Slic3r::Config::PrintObject->new });
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
        # detect how many perimeters must be generated for this island
        my $loop_number = $self->config->perimeters + ($surface->extra_perimeters || 0);
        $loop_number--;  # 0-indexed loops
        
        my @gaps = ();   # Polygons
        
        my @last = @{$surface->expolygon->simplify_p(&Slic3r::SCALED_RESOLUTION)};
        if ($loop_number >= 0) {  # no loops = -1
        
            my @contours    = ();   # depth => [ Polygon, Polygon ... ]
            my @holes       = ();   # depth => [ Polygon, Polygon ... ]
            my @thin_walls  = ();   # Polylines
        
            # we loop one time more than needed in order to find gaps after the last perimeter was applied
            for my $i (0..($loop_number+1)) {  # outer loop is 0
                my @offsets = ();
                if ($i == 0) {
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
                        my $diff = diff(
                            \@last,
                            offset(\@offsets, +0.5*$ext_pwidth),
                            1,  # medial axis requires non-overlapping geometry
                        );
                        
                        # the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
                        # (actually, something larger than that still may exist due to mitering or other causes)
                        my $min_width = $ext_pwidth / 4;
                        @thin_walls = @{offset2_ex($diff, -$min_width/2, +$min_width/2)};
                        
                        # the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
                        @thin_walls = grep $_->length > $ext_pwidth*2,
                            map @{$_->medial_axis($ext_pwidth + $ext_pspacing, $min_width)}, @thin_walls;
                        Slic3r::debugf "  %d thin walls detected\n", scalar(@thin_walls) if $Slic3r::debug;
        
                        if (0) {
                            require "Slic3r/SVG.pm";
                            Slic3r::SVG::output(
                                "medial_axis.svg",
                                no_arrows => 1,
                                expolygons      => union_ex($diff),
                                green_polylines => [ map $_->polygon->split_at_first_point, @{$self->perimeters} ],
                                red_polylines   => $self->_thin_wall_polylines,
                            );
                        }
                    }
                } else {
                    my $distance = ($i == 1) ? $ext_pspacing : $pspacing;
                    
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
                            offset(\@last, -0.5*$distance),
                            offset(\@offsets, +0.5*$distance + 10),  # safety offset
                        );
                        push @gaps, map $_->clone, map @$_, grep abs($_->area) >= $gap_area_threshold, @$diff;
                    }
                }
            
                last if !@offsets;
                last if $i > $loop_number; # we were only looking for gaps this time
                
                @last = @offsets;
                
                $contours[$i]   = [];
                $holes[$i]      = [];
                foreach my $polygon (@offsets) {
                    my $loop = Slic3r::Layer::PerimeterGenerator::Loop->new(
                        polygon     => $polygon,
                        is_contour  => $polygon->is_counter_clockwise,
                        depth       => $i,
                    );
                    if ($loop->is_contour) {
                        push @{$contours[$i]}, $loop;
                    } else {
                        push @{$holes[$i]}, $loop;
                    }
                }
            }
            
            # nest loops: holes first
            for my $d (0..$loop_number) {
                # loop through all holes having depth $d
                LOOP: for (my $i = 0; $i <= $#{$holes[$d]}; ++$i) {
                    my $loop = $holes[$d][$i];
                
                    # find the hole loop that contains this one, if any
                    for my $t (($d+1)..$loop_number) {
                        for (my $j = 0; $j <= $#{$holes[$t]}; ++$j) {
                            my $candidate_parent = $holes[$t][$j];
                            if ($candidate_parent->polygon->contains_point($loop->polygon->first_point)) {
                                $candidate_parent->add_child($loop);
                                splice @{$holes[$d]}, $i, 1;
                                --$i;
                                next LOOP;
                            }
                        }
                    }
                
                    # if no hole contains this hole, find the contour loop that contains it
                    for my $t (reverse 0..$loop_number) {
                        for (my $j = 0; $j <= $#{$contours[$t]}; ++$j) {
                            my $candidate_parent = $contours[$t][$j];
                            if ($candidate_parent->polygon->contains_point($loop->polygon->first_point)) {
                                $candidate_parent->add_child($loop);
                                splice @{$holes[$d]}, $i, 1;
                                --$i;
                                next LOOP;
                            }
                        }
                    }
                }
            }
        
            # nest contour loops
            for my $d (reverse 1..$loop_number) {
                # loop through all contours having depth $d
                LOOP: for (my $i = 0; $i <= $#{$contours[$d]}; ++$i) {
                    my $loop = $contours[$d][$i];
                
                    # find the contour loop that contains it
                    for my $t (reverse 0..($d-1)) {
                        for (my $j = 0; $j <= $#{$contours[$t]}; ++$j) {
                            my $candidate_parent = $contours[$t][$j];
                            if ($candidate_parent->polygon->contains_point($loop->polygon->first_point)) {
                                $candidate_parent->add_child($loop);
                                splice @{$contours[$d]}, $i, 1;
                                --$i;
                                next LOOP;
                            }
                        }
                    }
                }
            }
        
            # at this point, all loops should be in $contours[0]
            my @entities = $self->_traverse_loops($contours[0], \@thin_walls);
            
            # if brim will be printed, reverse the order of perimeters so that
            # we continue inwards after having finished the brim
            # TODO: add test for perimeter order
            @entities = reverse @entities
                if $self->config->external_perimeters_first
                    || ($self->layer_id == 0 && $self->print_config->brim_width > 0);
        
            # append perimeters for this slice as a collection
            $self->loops->append(Slic3r::ExtrusionPath::Collection->new(@entities))
                if @entities;
        }
        
        # fill gaps
        if (@gaps) {
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output(
                    "gaps.svg",
                    expolygons => union_ex(\@gaps),
                );
            }
            
            # where $pwidth < thickness < 2*$pspacing, infill with width = 2*$pwidth
            # where 0.1*$pwidth < thickness < $pwidth, infill with width = 1*$pwidth
            my @gap_sizes = (
                [ $pwidth, 2*$pspacing, unscale 2*$pwidth ],
                [ 0.1*$pwidth, $pwidth, unscale 1*$pwidth ],
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
                @gaps = @{diff(\@gaps, \@filled)};  # prevent more gap fill here
            }
        }
        
        # create one more offset to be used as boundary for fill
        # we offset by half the perimeter spacing (to get to the actual infill boundary)
        # and then we offset back and forth by half the infill spacing to only consider the
        # non-collapsing regions
        my $inset = 0;
        if ($loop_number == 0) {
            # one loop
            $inset += $ext_pspacing/2;
        } elsif ($loop_number > 0) {
            # two or more loops
            $inset += $pspacing/2;
        }
        $inset -= $self->config->get_abs_value_over('infill_overlap', $pwidth);
        
        my $min_perimeter_infill_spacing = $ispacing * (1 - &Slic3r::INSET_OVERLAP_TOLERANCE);
        $self->fill_surfaces->append($_)
            for map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNAL),  # use a bogus surface type
                @{offset2_ex(
                    [ map @{$_->simplify_p(&Slic3r::SCALED_RESOLUTION)}, @{union_ex(\@last)} ],
                    -$inset -$min_perimeter_infill_spacing/2,
                    +$min_perimeter_infill_spacing/2,
                )};
    }
}

sub _traverse_loops {
    my ($self, $loops, $thin_walls) = @_;
    
    # loops is an arrayref of ::Loop objects
    # turn each one into an ExtrusionLoop object
    my $coll = Slic3r::ExtrusionPath::Collection->new;
    foreach my $loop (@$loops) {
        my $is_external = $loop->is_external;
        
        my ($role, $loop_role);
        if ($is_external) {
            $role = EXTR_ROLE_EXTERNAL_PERIMETER;
        } else {
            $role = EXTR_ROLE_PERIMETER;
        }
        if ($loop->is_internal_contour) {
            # Note that we set loop role to ContourInternalPerimeter
            # also when loop is both internal and external (i.e.
            # there's only one contour loop).
            $loop_role = EXTRL_ROLE_CONTOUR_INTERNAL_PERIMETER;
        } else {
            $loop_role = EXTR_ROLE_PERIMETER;
        }
        
        # detect overhanging/bridging perimeters
        my @paths = ();
        if ($self->config->overhangs && $self->layer_id > 0
            && !($self->object_config->support_material && $self->object_config->support_material_contact_distance == 0)) {
            # get non-overhang paths by intersecting this loop with the grown lower slices
            foreach my $polyline (@{ intersection_ppl([ $loop->polygon ], $self->_lower_slices_p) }) {
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
            foreach my $polyline (@{ diff_ppl([ $loop->polygon ], $self->_lower_slices_p) }) {
                push @paths, Slic3r::ExtrusionPath->new(
                    polyline        => $polyline,
                    role            => EXTR_ROLE_OVERHANG_PERIMETER,
                    mm3_per_mm      => $self->_mm3_per_mm_overhang,
                    width           => $self->overhang_flow->width,
                    height          => $self->overhang_flow->height,
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
                polyline        => $loop->polygon->split_at_first_point,
                role            => $role,
                mm3_per_mm      => ($is_external ? $self->_ext_mm3_per_mm : $self->_mm3_per_mm),
                width           => ($is_external ? $self->ext_perimeter_flow->width : $self->perimeter_flow->width),
                height          => $self->layer_height,
            );
        }
        my $eloop = Slic3r::ExtrusionLoop->new_from_paths(@paths);
        $eloop->role($loop_role);
        $coll->append($eloop);
    }
    
    # append thin walls to the nearest-neighbor search (only for first iteration)
    if (@$thin_walls) {
        foreach my $polyline (@$thin_walls) {
            $coll->append(Slic3r::ExtrusionPath->new(
                polyline        => $polyline,
                role            => EXTR_ROLE_EXTERNAL_PERIMETER,
                mm3_per_mm      => $self->_mm3_per_mm,
                width           => $self->perimeter_flow->width,
                height          => $self->layer_height,
            ));
        }
        
        @$thin_walls = ();
    }
    
    # sort entities
    my $sorted_coll = $coll->chained_path_indices(0);
    my @indices = @{$sorted_coll->orig_indices};
    
    # traverse children
    my @entities = ();
    for my $i (0..$#indices) {
        my $idx = $indices[$i];
        if ($idx > $#$loops) {
            # this is a thin wall
            # let's get it from the sorted collection as it might have been reversed
            push @entities, $sorted_coll->[$i]->clone;
        } else {
            my $loop = $loops->[$idx];
            my $eloop = $coll->[$idx]->clone;
        
            my @children = $self->_traverse_loops($loop->children, $thin_walls);
            if ($loop->is_contour) {
                $eloop->make_counter_clockwise;
                push @entities, @children, $eloop;
            } else {
                $eloop->make_clockwise;
                push @entities, $eloop, @children;
            }
        }
    }
    return @entities;
}

sub _fill_gaps {
    my ($self, $min, $max, $w, $gaps) = @_;
    
    $min *= (1 - &Slic3r::INSET_OVERLAP_TOLERANCE);
    
    my $this = diff_ex(
        offset2($gaps, -$min/2, +$min/2),
        offset2($gaps, -$max/2, +$max/2),
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


package Slic3r::Layer::PerimeterGenerator::Loop;
use Moo;

has 'polygon'       => (is => 'ro', required => 1);
has 'is_contour'    => (is => 'ro', required => 1);
has 'depth'         => (is => 'ro', required => 1);
has 'children'      => (is => 'ro', default => sub { [] });

use List::Util qw(first);

sub add_child {
    my ($self, $child) = @_;
    push @{$self->children}, $child;
}

sub is_external {
    my ($self) = @_;
    return $self->depth == 0;
}

sub is_internal_contour {
    my ($self) = @_;
    
    if ($self->is_contour) {
        # an internal contour is a contour containing no other contours
        return !defined first { $_->is_contour } @{$self->children};
    }
    return 0;
}

1;
