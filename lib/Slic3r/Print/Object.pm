package Slic3r::Print::Object;
use strict;
use warnings;

use List::Util qw(min max sum first);
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(X Y Z PI scale unscale deg2rad rad2deg scaled_epsilon chained_path);
use Slic3r::Geometry::Clipper qw(diff diff_ex intersection intersection_ex union union_ex 
    offset offset_ex offset2 offset2_ex CLIPPER_OFFSET_SCALE JT_MITER);
use Slic3r::Print::State ':steps';
use Slic3r::Surface ':types';


# TODO: lazy
sub fill_maker {
    my $self = shift;
    return Slic3r::Fill->new(bounding_box => $self->bounding_box);
}

sub region_volumes {
    my $self = shift;
    return [ map $self->get_region_volumes($_), 0..($self->region_count - 1) ];
}

sub layers {
    my $self = shift;
    return [ map $self->get_layer($_), 0..($self->layer_count - 1) ];
}

sub support_layers {
    my $self = shift;
    return [ map $self->get_support_layer($_), 0..($self->support_layer_count - 1) ];
}

# TODO: translate to C++, then call it from constructor (see also
    # Print->add_model_object)
sub _trigger_copies {
    my $self = shift;
    
    # TODO: should this mean point is 0,0?
    return if !defined $self->_copies_shift;
    
    # order copies with a nearest neighbor search and translate them by _copies_shift
    $self->set_shifted_copies([
        map {
            my $c = $_->clone;
            $c->translate(@{ $self->_copies_shift });
            $c;
        } @{$self->copies}[@{chained_path($self->copies)}]
    ]);
    
    $self->print->invalidate_step(STEP_SKIRT);
    $self->print->invalidate_step(STEP_BRIM);
}

# in unscaled coordinates
sub add_copy {
    my ($self, $x, $y) = @_;
    my @copies = @{$self->copies};
    push @copies, Slic3r::Point->new_scale($x, $y);
    $self->set_copies(\@copies);
    $self->_trigger_copies;
}

sub delete_last_copy {
    my ($self) = @_;
    my @copies = $self->copies;
    pop @copies;
    $self->set_copies(\@copies);
    $self->_trigger_copies;
}

sub delete_all_copies {
    my ($self) = @_;
    $self->set_copies([]);
    $self->_trigger_copies;
}

# this is the *total* layer count (including support layers)
# this value is not supposed to be compared with $layer->id
# since they have different semantics
sub total_layer_count {
    my $self = shift;
    return $self->layer_count + $self->support_layer_count;
}

sub bounding_box {
    my $self = shift;
    
    # since the object is aligned to origin, bounding box coincides with size
    return Slic3r::Geometry::BoundingBox->new_from_points([
        Slic3r::Point->new(0,0),
        map Slic3r::Point->new($_->x, $_->y), $self->size  #))
    ]);
}

# this should be idempotent
sub slice {
    my $self = shift;
    
    return if $self->step_done(STEP_SLICE);
    $self->set_step_started(STEP_SLICE);
    $self->print->status_cb->(10, "Processing triangulated mesh");
    
    # init layers
    {
        $self->clear_layers;
    
        # make layers taking custom heights into account
        my $print_z = my $slice_z = my $height = my $id = 0;
        my $first_object_layer_height = -1;
        my $first_object_layer_distance = -1;
    
        # add raft layers
        if ($self->config->raft_layers > 0) {
            $id += $self->config->raft_layers;
        
            # raise first object layer Z by the thickness of the raft itself
            # plus the extra distance required by the support material logic
            $print_z += $self->config->get_value('first_layer_height');
            $print_z += $self->config->layer_height * ($self->config->raft_layers - 1);
        
            # at this stage we don't know which nozzles are actually used for the first layer
            # so we compute the average of all of them
            my $nozzle_diameter = sum(@{$self->print->config->nozzle_diameter})/@{$self->print->config->nozzle_diameter};
            my $distance = Slic3r::Print::SupportMaterial::contact_distance($nozzle_diameter);
        
            # force first layer print_z according to the contact distance
            # (the loop below will raise print_z by such height)
            $first_object_layer_height = $nozzle_diameter;
            $first_object_layer_distance = $distance;
        }
    
        # loop until we have at least one layer and the max slice_z reaches the object height
        my $max_z = unscale($self->size->z);
        while (($slice_z - $height) <= $max_z) {
            # assign the default height to the layer according to the general settings
            $height = ($id == 0)
                ? $self->config->get_value('first_layer_height')
                : $self->config->layer_height;
        
            # look for an applicable custom range
            if (my $range = first { $_->[0] <= $slice_z && $_->[1] > $slice_z } @{$self->layer_height_ranges}) {
                $height = $range->[2];
        
                # if user set custom height to zero we should just skip the range and resume slicing over it
                if ($height == 0) {
                    $slice_z += $range->[1] - $range->[0];
                    next;
                }
            }
            
            if ($first_object_layer_height != -1 && !@{$self->layers}) {
                $height = $first_object_layer_height;
                $print_z += ($first_object_layer_distance - $height);
            }
            
            $print_z += $height;
            $slice_z += $height/2;
        
            ### Slic3r::debugf "Layer %d: height = %s; slice_z = %s; print_z = %s\n", $id, $height, $slice_z, $print_z;
        
            $self->add_layer($id, $height, $print_z, $slice_z);
            if ($self->layer_count >= 2) {
                my $lc = $self->layer_count;
                $self->get_layer($lc - 2)->set_upper_layer($self->get_layer($lc - 1));
                $self->get_layer($lc - 1)->set_lower_layer($self->get_layer($lc - 2));
            }
            $id++;
        
            $slice_z += $height/2;   # add the other half layer
        }
    }
    
    # make sure all layers contain layer region objects for all regions
    my $regions_count = $self->print->region_count;
    foreach my $layer (@{ $self->layers }) {
        $layer->region($_) for 0 .. ($regions_count-1);
    }
    
    # get array of Z coordinates for slicing
    my @z = map $_->slice_z, @{$self->layers};
    
    # slice all non-modifier volumes
    for my $region_id (0..($self->region_count - 1)) {
        my $expolygons_by_layer = $self->_slice_region($region_id, \@z, 0);
        for my $layer_id (0..$#$expolygons_by_layer) {
            my $layerm = $self->get_layer($layer_id)->regions->[$region_id];
            $layerm->slices->clear;
            foreach my $expolygon (@{ $expolygons_by_layer->[$layer_id] }) {
                $layerm->slices->append(Slic3r::Surface->new(
                    expolygon    => $expolygon,
                    surface_type => S_TYPE_INTERNAL,
                ));
            }
        }
    }
    
    # then slice all modifier volumes
    if ($self->region_count > 1) {
        for my $region_id (0..$self->region_count) {
            my $expolygons_by_layer = $self->_slice_region($region_id, \@z, 1);
            
            # loop through the other regions and 'steal' the slices belonging to this one
            for my $other_region_id (0..$self->region_count) {
                next if $other_region_id == $region_id;
                
                for my $layer_id (0..$#$expolygons_by_layer) {
                    my $layerm = $self->get_layer($layer_id)->regions->[$region_id];
                    my $other_layerm = $self->get_layer($layer_id)->regions->[$other_region_id];
                    next if !defined $other_layerm;
                    
                    my $other_slices = [ map $_->p, @{$other_layerm->slices} ];  # Polygons
                    my $my_parts = intersection_ex(
                        $other_slices,
                        [ map @$_, @{ $expolygons_by_layer->[$layer_id] } ],
                    );
                    next if !@$my_parts;
                    
                    # append new parts to our region
                    foreach my $expolygon (@$my_parts) {
                        $layerm->slices->append(Slic3r::Surface->new(
                            expolygon    => $expolygon,
                            surface_type => S_TYPE_INTERNAL,
                        ));
                    }
                    
                    # remove such parts from original region
                    $other_layerm->slices->clear;
                    $other_layerm->slices->append(Slic3r::Surface->new(
                        expolygon    => $_,
                        surface_type => S_TYPE_INTERNAL,
                    )) for @{ diff_ex($other_slices, [ map @$_, @$my_parts ]) };
                }
            }
        }
    }
    
    # remove last layer(s) if empty
    $self->delete_layer($self->layer_count - 1)
        while $self->layer_count && (!map @{$_->slices}, @{$self->get_layer($self->layer_count - 1)->regions});
    
    foreach my $layer (@{ $self->layers }) {
        # apply size compensation
        if ($self->config->xy_size_compensation != 0) {
            my $delta = scale($self->config->xy_size_compensation);
            if (@{$layer->regions} == 1) {
                # single region
                my $layerm = $layer->regions->[0];
                my $slices = [ map $_->p, @{$layerm->slices} ];
                $layerm->slices->clear;
                $layerm->slices->append(Slic3r::Surface->new(
                    expolygon    => $_,
                    surface_type => S_TYPE_INTERNAL,
                )) for @{offset_ex($slices, $delta)};
            } else {
                if ($delta < 0) {
                    # multiple regions, shrinking
                    # we apply the offset to the combined shape, then intersect it
                    # with the original slices for each region
                    my $slices = union([ map $_->p, map @{$_->slices}, @{$layer->regions} ]);
                    $slices = offset($slices, $delta);
                    foreach my $layerm (@{$layer->regions}) {
                        my $this_slices = intersection_ex(
                            $slices,
                            [ map $_->p, @{$layerm->slices} ],
                        );
                        $layerm->slices->clear;
                        $layerm->slices->append(Slic3r::Surface->new(
                            expolygon    => $_,
                            surface_type => S_TYPE_INTERNAL,
                        )) for @$this_slices;
                    }
                } else {
                    # multiple regions, growing
                    # this is an ambiguous case, since it's not clear how to grow regions where they are going to overlap
                    # so we give priority to the first one and so on
                    for my $i (0..$#{$layer->regions}) {
                        my $layerm = $layer->regions->[$i];
                        my $slices = offset_ex([ map $_->p, @{$layerm->slices} ], $delta);
                        if ($i > 0) {
                            $slices = diff_ex(
                                [ map @$_, @$slices ],
                                [ map $_->p, map @{$_->slices}, map $layer->regions->[$_], 0..($i-1) ],  # slices of already processed regions
                            );
                        }
                        $layerm->slices->clear;
                        $layerm->slices->append(Slic3r::Surface->new(
                            expolygon    => $_,
                            surface_type => S_TYPE_INTERNAL,
                        )) for @$slices;
                    }
                }
            }
        }
        
        # merge all regions' slices to get islands
        $layer->make_slices;
    }
    
    # detect slicing errors
    my $warning_thrown = 0;
    for my $i (0 .. ($self->layer_count - 1)) {
        my $layer = $self->get_layer($i);
        next unless $layer->slicing_errors;
        if (!$warning_thrown) {
            warn "The model has overlapping or self-intersecting facets. I tried to repair it, "
                . "however you might want to check the results or repair the input file and retry.\n";
            $warning_thrown = 1;
        }
        
        # try to repair the layer surfaces by merging all contours and all holes from
        # neighbor layers
        Slic3r::debugf "Attempting to repair layer %d\n", $i;
        
        foreach my $region_id (0 .. ($layer->region_count - 1)) {
            my $layerm = $layer->region($region_id);
            
            my (@upper_surfaces, @lower_surfaces);
            for (my $j = $i+1; $j < $self->layer_count; $j++) {
                if (!$self->get_layer($j)->slicing_errors) {
                    @upper_surfaces = @{$self->get_layer($j)->region($region_id)->slices};
                    last;
                }
            }
            for (my $j = $i-1; $j >= 0; $j--) {
                if (!$self->get_layer($j)->slicing_errors) {
                    @lower_surfaces = @{$self->get_layer($j)->region($region_id)->slices};
                    last;
                }
            }
            
            my $union = union_ex([
                map $_->expolygon->contour, @upper_surfaces, @lower_surfaces,
            ]);
            my $diff = diff_ex(
                [ map @$_, @$union ],
                [ map @{$_->expolygon->holes}, @upper_surfaces, @lower_surfaces, ],
            );
            
            $layerm->slices->clear;
            $layerm->slices->append(
                map Slic3r::Surface->new
                    (expolygon => $_, surface_type => S_TYPE_INTERNAL),
                    @$diff
            );
        }
            
        # update layer slices after repairing the single regions
        $layer->make_slices;
    }
    
    # remove empty layers from bottom
    while (@{$self->layers} && !@{$self->get_layer(0)->slices}) {
        shift @{$self->layers};
        for (my $i = 0; $i <= $#{$self->layers}; $i++) {
            $self->get_layer($i)->id( $self->get_layer($i)->id-1 );
        }
    }
    
    # simplify slices if required
    if ($self->print->config->resolution) {
        $self->_simplify_slices(scale($self->print->config->resolution));
    }
    
    die "No layers were detected. You might want to repair your STL file(s) or check their size and retry.\n"
        if !@{$self->layers};
    
    $self->set_typed_slices(0);
    $self->set_step_done(STEP_SLICE);
}

sub _slice_region {
    my ($self, $region_id, $z, $modifier) = @_;

    return [] if !@{$self->get_region_volumes($region_id)};

    # compose mesh
    my $mesh;
    foreach my $volume_id (@{ $self->get_region_volumes($region_id) }) {
        my $volume = $self->model_object->volumes->[$volume_id];
        next if $volume->modifier && !$modifier;
        next if !$volume->modifier && $modifier;
        
        if (defined $mesh) {
            $mesh->merge($volume->mesh);
        } else {
            $mesh = $volume->mesh->clone;
        }
    }
    return if !defined $mesh;

    # transform mesh
    # we ignore the per-instance transformations currently and only 
    # consider the first one
    $self->model_object->instances->[0]->transform_mesh($mesh, 1);

    # align mesh to Z = 0 and apply XY shift
    $mesh->translate((map unscale(-$_), @{$self->_copies_shift}), -$self->model_object->bounding_box->z_min);
    
    # perform actual slicing
    return $mesh->slice($z);
}

sub make_perimeters {
    my $self = shift;
    
    # prerequisites
    $self->print->init_extruders;
    $self->slice;
    
    return if $self->step_done(STEP_PERIMETERS);
    $self->set_step_started(STEP_PERIMETERS);
    $self->print->status_cb->(20, "Generating perimeters");
    
    # merge slices if they were split into types
    if ($self->typed_slices) {
        $_->merge_slices for @{$self->layers};
        $self->set_typed_slices(0);
        $self->invalidate_step(STEP_PREPARE_INFILL);
    }
    
    # compare each layer to the one below, and mark those slices needing
    # one additional inner perimeter, like the top of domed objects-
    
    # this algorithm makes sure that at least one perimeter is overlapping
    # but we don't generate any extra perimeter if fill density is zero, as they would be floating
    # inside the object - infill_only_where_needed should be the method of choice for printing
    # hollow objects
    for my $region_id (0 .. ($self->print->region_count-1)) {
        my $region = $self->print->regions->[$region_id];
        my $region_perimeters = $region->config->perimeters;
        
        if ($region->config->extra_perimeters && $region_perimeters > 0 && $region->config->fill_density > 0) {
            for my $i (0 .. ($self->layer_count - 2)) {
                my $layerm          = $self->get_layer($i)->regions->[$region_id];
                my $upper_layerm    = $self->get_layer($i+1)->regions->[$region_id];
                my $perimeter_spacing       = $layerm->flow(FLOW_ROLE_PERIMETER)->scaled_spacing;
                my $ext_perimeter_spacing   = $layerm->flow(FLOW_ROLE_EXTERNAL_PERIMETER)->scaled_spacing;
                
                my $overlap = $perimeter_spacing;  # one perimeter
                
                my $diff = diff(
                    offset([ map @{$_->expolygon}, @{$layerm->slices} ], -($ext_perimeter_spacing + ($region_perimeters-1) * $perimeter_spacing)),
                    offset([ map @{$_->expolygon}, @{$upper_layerm->slices} ], -$overlap),
                );
                next if !@$diff;
                # if we need more perimeters, $diff should contain a narrow region that we can collapse
                
                # we use a higher miterLimit here to handle areas with acute angles
                # in those cases, the default miterLimit would cut the corner and we'd
                # get a triangle that would trigger a non-needed extra perimeter
                $diff = diff(
                    $diff,
                    offset2($diff, -$perimeter_spacing, +$perimeter_spacing, CLIPPER_OFFSET_SCALE, JT_MITER, 5),
                    1,
                );
                next if !@$diff;
                # diff contains the collapsed area
                
                foreach my $slice (@{$layerm->slices}) {
                    my $extra_perimeters = 0;
                    CYCLE: while (1) {
                        # compute polygons representing the thickness of the hypotetical new internal perimeter
                        # of our slice
                        $extra_perimeters++;
                        my $hypothetical_perimeter = diff(
                            offset($slice->expolygon->arrayref, -($perimeter_spacing * ($region_perimeters + $extra_perimeters-1))),
                            offset($slice->expolygon->arrayref, -($perimeter_spacing * ($region_perimeters + $extra_perimeters))),
                        );
                        last CYCLE if !@$hypothetical_perimeter;  # no extra perimeter is possible
                        
                        # only add the perimeter if there's an intersection with the collapsed area
                        last CYCLE if !@{ intersection($diff, $hypothetical_perimeter) };
                        Slic3r::debugf "  adding one more perimeter at layer %d\n", $layerm->id;
                        $slice->extra_perimeters($extra_perimeters);
                    }
                }
            }
        }
    }
    
    Slic3r::parallelize(
        threads => $self->print->config->threads,
        items => sub { 0 .. ($self->layer_count - 1) },
        thread_cb => sub {
            my $q = shift;
            while (defined (my $i = $q->dequeue)) {
                $self->get_layer($i)->make_perimeters;
            }
        },
        collect_cb => sub {},
        no_threads_cb => sub {
            $_->make_perimeters for @{$self->layers};
        },
    );
    
    # simplify slices (both layer and region slices),
    # we only need the max resolution for perimeters
    ### This makes this method not-idempotent, so we keep it disabled for now.
    ###$self->_simplify_slices(&Slic3r::SCALED_RESOLUTION);
    
    $self->set_step_done(STEP_PERIMETERS);
}

sub prepare_infill {
    my ($self) = @_;
    
    # prerequisites
    $self->make_perimeters;
    
    return if $self->step_done(STEP_PREPARE_INFILL);
    $self->set_step_started(STEP_PREPARE_INFILL);
    $self->print->status_cb->(30, "Preparing infill");
    
    # this will assign a type (top/bottom/internal) to $layerm->slices
    # and transform $layerm->fill_surfaces from expolygon 
    # to typed top/bottom/internal surfaces;
    $self->detect_surfaces_type;
    $self->set_typed_slices(1);
    
    # decide what surfaces are to be filled
    $_->prepare_fill_surfaces for map @{$_->regions}, @{$self->layers};

    # this will detect bridges and reverse bridges
    # and rearrange top/bottom/internal surfaces
    $self->process_external_surfaces;

    # detect which fill surfaces are near external layers
    # they will be split in internal and internal-solid surfaces
    $self->discover_horizontal_shells;
    $self->clip_fill_surfaces;
    
    # the following step needs to be done before combination because it may need
    # to remove only half of the combined infill
    $self->bridge_over_infill;

    # combine fill surfaces to honor the "infill every N layers" option
    $self->combine_infill;
    
    $self->set_step_done(STEP_PREPARE_INFILL);
}

sub infill {
    my ($self) = @_;
    
    # prerequisites
    $self->prepare_infill;
    
    return if $self->step_done(STEP_INFILL);
    $self->set_step_started(STEP_INFILL);
    $self->print->status_cb->(70, "Infilling layers");
    
    Slic3r::parallelize(
        threads => $self->print->config->threads,
        items => sub {
            my @items = ();  # [layer_id, region_id]
            for my $region_id (0 .. ($self->print->region_count-1)) {
                push @items, map [$_, $region_id], 0..($self->layer_count - 1);
            }
            @items;
        },
        thread_cb => sub {
            my $q = shift;
            while (defined (my $obj_layer = $q->dequeue)) {
                my ($i, $region_id) = @$obj_layer;
                my $layerm = $self->get_layer($i)->regions->[$region_id];
                $layerm->fills->clear;
                $layerm->fills->append( $self->fill_maker->make_fill($layerm) );
            }
        },
        collect_cb => sub {},
        no_threads_cb => sub {
            foreach my $layerm (map @{$_->regions}, @{$self->layers}) {
                $layerm->fills->clear;
                $layerm->fills->append($self->fill_maker->make_fill($layerm));
            }
        },
    );

    ### we could free memory now, but this would make this step not idempotent
    ### $_->fill_surfaces->clear for map @{$_->regions}, @{$object->layers};
    
    $self->set_step_done(STEP_INFILL);
}

sub generate_support_material {
    my $self = shift;
    
    # prerequisites
    $self->print->init_extruders;
    $self->slice;
    
    return if $self->step_done(STEP_SUPPORTMATERIAL);
    $self->set_step_started(STEP_SUPPORTMATERIAL);
    
    $self->clear_support_layers;
    
    if ((!$self->config->support_material && $self->config->raft_layers == 0) || scalar(@{$self->layers}) < 2) {
        $self->set_step_done(STEP_SUPPORTMATERIAL);
        return;
    }
    $self->print->status_cb->(85, "Generating support material");
    
    my $first_layer_flow = Slic3r::Flow->new_from_width(
        width               => ($self->config->first_layer_extrusion_width || $self->config->support_material_extrusion_width),
        role                => FLOW_ROLE_SUPPORT_MATERIAL,
        nozzle_diameter     => $self->print->config->nozzle_diameter->[ $self->config->support_material_extruder-1 ]
                                // $self->print->config->nozzle_diameter->[0],
        layer_height        => $self->config->get_abs_value('first_layer_height'),
        bridge_flow_ratio   => 0,
    );
    
    my $s = Slic3r::Print::SupportMaterial->new(
        print_config        => $self->print->config,
        object_config       => $self->config,
        first_layer_flow    => $first_layer_flow,
        flow                => $self->support_material_flow,
        interface_flow      => $self->support_material_flow(FLOW_ROLE_SUPPORT_MATERIAL_INTERFACE),
    );
    $s->generate($self);
    
    $self->set_step_done(STEP_SUPPORTMATERIAL);
}

sub detect_surfaces_type {
    my $self = shift;
    Slic3r::debugf "Detecting solid surfaces...\n";
    
    for my $region_id (0 .. ($self->print->region_count-1)) {
        for my $i (0 .. ($self->layer_count - 1)) {
            my $layerm = $self->get_layer($i)->regions->[$region_id];
        
            # prepare a reusable subroutine to make surface differences
            my $difference = sub {
                my ($subject, $clip, $result_type) = @_;
                my $diff = diff(
                    [ map @$_, @$subject ],
                    [ map @$_, @$clip ],
                );
                
                # collapse very narrow parts (using the safety offset in the diff is not enough)
                my $offset = $layerm->flow(FLOW_ROLE_EXTERNAL_PERIMETER)->scaled_width / 10;
                return map Slic3r::Surface->new(expolygon => $_, surface_type => $result_type),
                    @{ offset2_ex($diff, -$offset, +$offset) };
            };
            
            # comparison happens against the *full* slices (considering all regions)
            # unless internal shells are requested
            my $upper_layer = $i < $self->layer_count - 1 ? $self->get_layer($i+1) : undef;
            my $lower_layer = $i > 0 ? $self->get_layer($i-1) : undef;
            
            # find top surfaces (difference between current surfaces
            # of current layer and upper one)
            my @top = ();
            if ($upper_layer) {
                my $upper_slices = $self->config->interface_shells
                    ? [ map $_->expolygon, @{$upper_layer->regions->[$region_id]->slices} ]
                    : $upper_layer->slices;
                
                @top = $difference->(
                    [ map $_->expolygon, @{$layerm->slices} ],
                    $upper_slices,
                    S_TYPE_TOP,
                );
            } else {
                # if no upper layer, all surfaces of this one are solid
                # we clone surfaces because we're going to clear the slices collection
                @top = map $_->clone, @{$layerm->slices};
                $_->surface_type(S_TYPE_TOP) for @top;
            }
            
            # find bottom surfaces (difference between current surfaces
            # of current layer and lower one)
            my @bottom = ();
            if ($lower_layer) {
                # any surface lying on the void is a true bottom bridge
                push @bottom, $difference->(
                    [ map $_->expolygon, @{$layerm->slices} ],
                    $lower_layer->slices,
                    S_TYPE_BOTTOMBRIDGE,
                );
                
                # if user requested internal shells, we need to identify surfaces
                # lying on other slices not belonging to this region
                if ($self->config->interface_shells) {
                    # non-bridging bottom surfaces: any part of this layer lying 
                    # on something else, excluding those lying on our own region
                    my $supported = intersection_ex(
                        [ map @{$_->expolygon}, @{$layerm->slices} ],
                        [ map @$_, @{$lower_layer->slices} ],
                    );
                    push @bottom, $difference->(
                        $supported,
                        [ map $_->expolygon, @{$lower_layer->regions->[$region_id]->slices} ],
                        S_TYPE_BOTTOM,
                    );
                }
            } else {
                # if no lower layer, all surfaces of this one are solid
                # we clone surfaces because we're going to clear the slices collection
                @bottom = map $_->clone, @{$layerm->slices};
                $_->surface_type(S_TYPE_BOTTOM) for @bottom;
            }
            
            # now, if the object contained a thin membrane, we could have overlapping bottom
            # and top surfaces; let's do an intersection to discover them and consider them
            # as bottom surfaces (to allow for bridge detection)
            if (@top && @bottom) {
                my $overlapping = intersection_ex([ map $_->p, @top ], [ map $_->p, @bottom ]);
                Slic3r::debugf "  layer %d contains %d membrane(s)\n", $layerm->id, scalar(@$overlapping)
                    if $Slic3r::debug;
                @top = $difference->([map $_->expolygon, @top], $overlapping, S_TYPE_TOP);
            }
            
            # find internal surfaces (difference between top/bottom surfaces and others)
            my @internal = $difference->(
                [ map $_->expolygon, @{$layerm->slices} ],
                [ map $_->expolygon, @top, @bottom ],
                S_TYPE_INTERNAL,
            );
            
            # save surfaces to layer
            $layerm->slices->clear;
            $layerm->slices->append(@bottom, @top, @internal);
            
            Slic3r::debugf "  layer %d has %d bottom, %d top and %d internal surfaces\n",
                $layerm->id, scalar(@bottom), scalar(@top), scalar(@internal) if $Slic3r::debug;
        }
        
        # clip surfaces to the fill boundaries
        foreach my $layer (@{$self->layers}) {
            my $layerm = $layer->regions->[$region_id];
            
            # Note: this method should be idempotent, but fill_surfaces gets modified 
            # in place. However we're now only using its boundaries (which are invariant)
            # so we're safe
            my $fill_boundaries = [ map $_->clone->p, @{$layerm->fill_surfaces} ];
            $layerm->fill_surfaces->clear;
            foreach my $surface (@{$layerm->slices}) {
                my $intersection = intersection_ex(
                    [ $surface->p ],
                    $fill_boundaries,
                );
                $layerm->fill_surfaces->append(map Slic3r::Surface->new
                    (expolygon => $_, surface_type => $surface->surface_type),
                    @$intersection);
            }
        }
    }
}

sub clip_fill_surfaces {
    my $self = shift;
    return unless $self->config->infill_only_where_needed;
    
    # We only want infill under ceilings; this is almost like an
    # internal support material.
    
    my $additional_margin = scale 3*0;
    
    my $overhangs = [];  # arrayref of polygons
    for my $layer_id (reverse 0..($self->layer_count - 1)) {
        my $layer = $self->get_layer($layer_id);
        my @layer_internal = ();  # arrayref of Surface objects
        my @new_internal = ();    # arrayref of Surface objects
        
        # clip this layer's internal surfaces to @overhangs
        foreach my $layerm (@{$layer->regions}) {
            # we assume that this step is run before bridge_over_infill() and combine_infill()
            # so these are the only internal types we might have
            my (@internal, @other) = ();
            foreach my $surface (map $_->clone, @{$layerm->fill_surfaces}) {
                $surface->surface_type == S_TYPE_INTERNAL
                    ? push @internal, $surface
                    : push @other, $surface;
            }
            
            # keep all the original internal surfaces to detect overhangs in this layer
            push @layer_internal, @internal;
            
            push @new_internal, my @new = map Slic3r::Surface->new(
                expolygon       => $_,
                surface_type    => S_TYPE_INTERNAL,
            ),
            @{intersection_ex(
                $overhangs,
                [ map $_->p, @internal ],
            )};
            
            $layerm->fill_surfaces->clear;
            $layerm->fill_surfaces->append(@new, @other);
        }
        
        # get this layer's overhangs defined as the full slice minus the internal infill
        # (thus we also consider perimeters)
        if ($layer_id > 0) {
            my $solid = diff(
                [ map $_->p, map @{$_->fill_surfaces}, @{$layer->regions} ],
                [ map $_->p, @layer_internal ],
            );
            $overhangs = offset($solid, +$additional_margin);
            
            push @$overhangs, map $_->p, @new_internal;  # propagate upper overhangs
        }
    }
}

sub bridge_over_infill {
    my $self = shift;
    
    for my $region_id (0..($self->print->region_count - 1)) {
        my $fill_density = $self->print->regions->[$region_id]->config->fill_density;
        next if $fill_density == 100 || $fill_density == 0;
        
        for my $layer_id (1..($self->layer_count - 1)) {
            my $layer       = $self->get_layer($layer_id);
            my $layerm      = $layer->regions->[$region_id];
            my $lower_layer = $self->get_layer($layer_id-1);
            
            # compute the areas needing bridge math 
            my @internal_solid = @{$layerm->fill_surfaces->filter_by_type(S_TYPE_INTERNALSOLID)};
            my @lower_internal = map @{$_->fill_surfaces->filter_by_type(S_TYPE_INTERNAL)}, @{$lower_layer->regions};
            my $to_bridge = intersection_ex(
                [ map $_->p, @internal_solid ],
                [ map $_->p, @lower_internal ],
            );
            next unless @$to_bridge;
            Slic3r::debugf "Bridging %d internal areas at layer %d\n", scalar(@$to_bridge), $layer_id;
            
            # build the new collection of fill_surfaces
            {
                my @new_surfaces = map $_->clone, grep $_->surface_type != S_TYPE_INTERNALSOLID, @{$layerm->fill_surfaces};
                push @new_surfaces, map Slic3r::Surface->new(
                        expolygon       => $_,
                        surface_type    => S_TYPE_INTERNALBRIDGE,
                    ), @$to_bridge;
                push @new_surfaces, map Slic3r::Surface->new(
                        expolygon       => $_,
                        surface_type    => S_TYPE_INTERNALSOLID,
                    ), @{diff_ex(
                        [ map $_->p, @internal_solid ],
                        [ map @$_, @$to_bridge ],
                        1,
                    )};
                $layerm->fill_surfaces->clear;
                $layerm->fill_surfaces->append(@new_surfaces);
            }
            
            # exclude infill from the layers below if needed
            # see discussion at https://github.com/alexrj/Slic3r/issues/240
            # Update: do not exclude any infill. Sparse infill is able to absorb the excess material.
            if (0) {
                my $excess = $layerm->extruders->{infill}->bridge_flow->width - $layerm->height;
                for (my $i = $layer_id-1; $excess >= $self->get_layer($i)->height; $i--) {
                    Slic3r::debugf "  skipping infill below those areas at layer %d\n", $i;
                    foreach my $lower_layerm (@{$self->get_layer($i)->regions}) {
                        my @new_surfaces = ();
                        # subtract the area from all types of surfaces
                        foreach my $group (@{$lower_layerm->fill_surfaces->group}) {
                            push @new_surfaces, map $group->[0]->clone(expolygon => $_),
                                @{diff_ex(
                                    [ map $_->p, @$group ],
                                    [ map @$_, @$to_bridge ],
                                )};
                            push @new_surfaces, map Slic3r::Surface->new(
                                expolygon       => $_,
                                surface_type    => S_TYPE_INTERNALVOID,
                            ), @{intersection_ex(
                                [ map $_->p, @$group ],
                                [ map @$_, @$to_bridge ],
                            )};
                        }
                        $lower_layerm->fill_surfaces->clear;
                        $lower_layerm->fill_surfaces->append(@new_surfaces);
                    }
                    
                    $excess -= $self->get_layer($i)->height;
                }
            }
        }
    }
}

sub process_external_surfaces {
    my ($self) = @_;
    
    for my $region_id (0 .. ($self->print->region_count-1)) {
        $self->get_layer(0)->regions->[$region_id]->process_external_surfaces(undef);
        for my $i (1 .. ($self->layer_count - 1)) {
            $self->get_layer($i)->regions->[$region_id]->process_external_surfaces($self->get_layer($i-1));
        }
    }
}

sub discover_horizontal_shells {
    my $self = shift;
    
    Slic3r::debugf "==> DISCOVERING HORIZONTAL SHELLS\n";
    
    for my $region_id (0 .. ($self->print->region_count-1)) {
        for (my $i = 0; $i < $self->layer_count; $i++) {
            my $layerm = $self->get_layer($i)->regions->[$region_id];
            
            if ($layerm->config->solid_infill_every_layers && $layerm->config->fill_density > 0
                && ($i % $layerm->config->solid_infill_every_layers) == 0) {
                $_->surface_type(S_TYPE_INTERNALSOLID) for @{$layerm->fill_surfaces->filter_by_type(S_TYPE_INTERNAL)};
            }
            
            EXTERNAL: foreach my $type (S_TYPE_TOP, S_TYPE_BOTTOM, S_TYPE_BOTTOMBRIDGE) {
                # find slices of current type for current layer
                # use slices instead of fill_surfaces because they also include the perimeter area
                # which needs to be propagated in shells; we need to grow slices like we did for
                # fill_surfaces though.  Using both ungrown slices and grown fill_surfaces will
                # not work in some situations, as there won't be any grown region in the perimeter 
                # area (this was seen in a model where the top layer had one extra perimeter, thus
                # its fill_surfaces were thinner than the lower layer's infill), however it's the best
                # solution so far. Growing the external slices by EXTERNAL_INFILL_MARGIN will put
                # too much solid infill inside nearly-vertical slopes.
                my $solid = [
                    (map $_->p, @{$layerm->slices->filter_by_type($type)}),
                    (map $_->p, @{$layerm->fill_surfaces->filter_by_type($type)}),
                ];
                next if !@$solid;
                Slic3r::debugf "Layer %d has %s surfaces\n", $i, ($type == S_TYPE_TOP) ? 'top' : 'bottom';
                
                my $solid_layers = ($type == S_TYPE_TOP)
                    ? $layerm->config->top_solid_layers
                    : $layerm->config->bottom_solid_layers;
                NEIGHBOR: for (my $n = ($type == S_TYPE_TOP) ? $i-1 : $i+1; 
                        abs($n - $i) <= $solid_layers-1; 
                        ($type == S_TYPE_TOP) ? $n-- : $n++) {
                    
                    next if $n < 0 || $n >= $self->layer_count;
                    Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;
                    
                    my $neighbor_layerm = $self->get_layer($n)->regions->[$region_id];
                    my $neighbor_fill_surfaces = $neighbor_layerm->fill_surfaces;
                    my @neighbor_fill_surfaces = map $_->clone, @$neighbor_fill_surfaces;  # clone because we will use these surfaces even after clearing the collection
                    
                    # find intersection between neighbor and current layer's surfaces
                    # intersections have contours and holes
                    # we update $solid so that we limit the next neighbor layer to the areas that were
                    # found on this one - in other words, solid shells on one layer (for a given external surface)
                    # are always a subset of the shells found on the previous shell layer
                    # this approach allows for DWIM in hollow sloping vases, where we want bottom
                    # shells to be generated in the base but not in the walls (where there are many
                    # narrow bottom surfaces): reassigning $solid will consider the 'shadow' of the 
                    # upper perimeter as an obstacle and shell will not be propagated to more upper layers
                    my $new_internal_solid = $solid = intersection(
                        $solid,
                        [ map $_->p, grep { ($_->surface_type == S_TYPE_INTERNAL) || ($_->surface_type == S_TYPE_INTERNALSOLID) } @neighbor_fill_surfaces ],
                        1,
                    );
                    next EXTERNAL if !@$new_internal_solid;
                    
                    if ($layerm->config->fill_density == 0) {
                        # if we're printing a hollow object we discard any solid shell thinner
                        # than a perimeter width, since it's probably just crossing a sloping wall
                        # and it's not wanted in a hollow print even if it would make sense when
                        # obeying the solid shell count option strictly (DWIM!)
                        my $margin = $neighbor_layerm->flow(FLOW_ROLE_EXTERNAL_PERIMETER)->scaled_width;
                        my $too_narrow = diff(
                            $new_internal_solid,
                            offset2($new_internal_solid, -$margin, +$margin, CLIPPER_OFFSET_SCALE, JT_MITER, 5),
                            1,
                        );
                        $new_internal_solid = $solid = diff(
                            $new_internal_solid,
                            $too_narrow,
                        ) if @$too_narrow;
                    }
                    
                    # make sure the new internal solid is wide enough, as it might get collapsed
                    # when spacing is added in Fill.pm
                    {
                        my $margin = 3 * $layerm->flow(FLOW_ROLE_SOLID_INFILL)->scaled_width; # require at least this size
                        # we use a higher miterLimit here to handle areas with acute angles
                        # in those cases, the default miterLimit would cut the corner and we'd
                        # get a triangle in $too_narrow; if we grow it below then the shell
                        # would have a different shape from the external surface and we'd still
                        # have the same angle, so the next shell would be grown even more and so on.
                        my $too_narrow = diff(
                            $new_internal_solid,
                            offset2($new_internal_solid, -$margin, +$margin, CLIPPER_OFFSET_SCALE, JT_MITER, 5),
                            1,
                        );
                        
                        if (@$too_narrow) {
                            # grow the collapsing parts and add the extra area to  the neighbor layer 
                            # as well as to our original surfaces so that we support this 
                            # additional area in the next shell too
                        
                            # make sure our grown surfaces don't exceed the fill area
                            my @grown = @{intersection(
                                offset($too_narrow, +$margin),
                                [ map $_->p, @neighbor_fill_surfaces ],
                            )};
                            $new_internal_solid = $solid = [ @grown, @$new_internal_solid ];
                        }
                    }
                    
                    # internal-solid are the union of the existing internal-solid surfaces
                    # and new ones
                    my $internal_solid = union_ex([
                        ( map $_->p, grep $_->surface_type == S_TYPE_INTERNALSOLID, @neighbor_fill_surfaces ),
                        @$new_internal_solid,
                    ]);
                    
                    # subtract intersections from layer surfaces to get resulting internal surfaces
                    my $internal = diff_ex(
                        [ map $_->p, grep $_->surface_type == S_TYPE_INTERNAL, @neighbor_fill_surfaces ],
                        [ map @$_, @$internal_solid ],
                        1,
                    );
                    Slic3r::debugf "    %d internal-solid and %d internal surfaces found\n",
                        scalar(@$internal_solid), scalar(@$internal);
                    
                    # assign resulting internal surfaces to layer
                    $neighbor_fill_surfaces->clear;
                    $neighbor_fill_surfaces->append(map Slic3r::Surface->new
                        (expolygon => $_, surface_type => S_TYPE_INTERNAL), @$internal);
                    
                    # assign new internal-solid surfaces to layer
                    $neighbor_fill_surfaces->append(map Slic3r::Surface->new
                        (expolygon => $_, surface_type => S_TYPE_INTERNALSOLID), @$internal_solid);
                    
                    # assign top and bottom surfaces to layer
                    foreach my $s (@{Slic3r::Surface::Collection->new(grep { ($_->surface_type == S_TYPE_TOP) || $_->is_bottom } @neighbor_fill_surfaces)->group}) {
                        my $solid_surfaces = diff_ex(
                            [ map $_->p, @$s ],
                            [ map @$_, @$internal_solid, @$internal ],
                            1,
                        );
                        $neighbor_fill_surfaces->append(map $s->[0]->clone(expolygon => $_), @$solid_surfaces);
                    }
                }
            }
        }
    }
}

# combine fill surfaces across layers
sub combine_infill {
    my $self = shift;
    
    return unless defined first { $_->config->infill_every_layers > 1 && $_->config->fill_density > 0 } @{$self->print->regions};
    
    my @layer_heights = map $_->height, @{$self->layers};
    
    for my $region_id (0 .. ($self->print->region_count-1)) {
        my $region = $self->print->regions->[$region_id];
        my $every = $region->config->infill_every_layers;
        
        # limit the number of combined layers to the maximum height allowed by this regions' nozzle
        my $nozzle_diameter = $self->print->config->get_at('nozzle_diameter', $region->config->infill_extruder-1);
        
        # define the combinations
        my @combine = ();   # layer_id => thickness in layers
        {
            my $current_height = my $layers = 0;
            for my $layer_id (1 .. $#layer_heights) {
                my $height = $self->get_layer($layer_id)->height;
                
                if ($current_height + $height >= $nozzle_diameter || $layers >= $every) {
                    $combine[$layer_id-1] = $layers;
                    $current_height = $layers = 0;
                }
                
                $current_height += $height;
                $layers++;
            }
        }
        
        # skip bottom layer
        for my $layer_id (1 .. $#combine) {
            next unless ($combine[$layer_id] // 1) > 1;
            my @layerms = map $self->get_layer($_)->regions->[$region_id],
                ($layer_id - ($combine[$layer_id]-1) .. $layer_id);
            
            # only combine internal infill
            for my $type (S_TYPE_INTERNAL) {
                # we need to perform a multi-layer intersection, so let's split it in pairs
                
                # initialize the intersection with the candidates of the lowest layer
                my $intersection = [ map $_->expolygon, @{$layerms[0]->fill_surfaces->filter_by_type($type)} ];
                
                # start looping from the second layer and intersect the current intersection with it
                for my $layerm (@layerms[1 .. $#layerms]) {
                    $intersection = intersection_ex(
                        [ map @$_, @$intersection ],
                        [ map @{$_->expolygon}, @{$layerm->fill_surfaces->filter_by_type($type)} ],
                    );
                }
                
                my $area_threshold = $layerms[0]->infill_area_threshold;
                @$intersection = grep $_->area > $area_threshold, @$intersection;
                next if !@$intersection;
                Slic3r::debugf "  combining %d %s regions from layers %d-%d\n",
                    scalar(@$intersection),
                    ($type == S_TYPE_INTERNAL ? 'internal' : 'internal-solid'),
                    $layer_id-($every-1), $layer_id;
                
                # $intersection now contains the regions that can be combined across the full amount of layers
                # so let's remove those areas from all layers
                
                 my @intersection_with_clearance = map @{$_->offset(
                       $layerms[-1]->flow(FLOW_ROLE_SOLID_INFILL)->scaled_width    / 2
                     + $layerms[-1]->flow(FLOW_ROLE_PERIMETER)->scaled_width / 2
                     # Because fill areas for rectilinear and honeycomb are grown 
                     # later to overlap perimeters, we need to counteract that too.
                     + (($type == S_TYPE_INTERNALSOLID || $region->config->fill_pattern =~ /(rectilinear|honeycomb)/)
                       ? $layerms[-1]->flow(FLOW_ROLE_SOLID_INFILL)->scaled_width * &Slic3r::INFILL_OVERLAP_OVER_SPACING
                       : 0)
                     )}, @$intersection;

                
                foreach my $layerm (@layerms) {
                    my @this_type   = @{$layerm->fill_surfaces->filter_by_type($type)};
                    my @other_types = map $_->clone, grep $_->surface_type != $type, @{$layerm->fill_surfaces};
                    
                    my @new_this_type = map Slic3r::Surface->new(expolygon => $_, surface_type => $type),
                        @{diff_ex(
                            [ map $_->p, @this_type ],
                            [ @intersection_with_clearance ],
                        )};
                    
                    # apply surfaces back with adjusted depth to the uppermost layer
                    if ($layerm->id == $layer_id) {
                        push @new_this_type,
                            map Slic3r::Surface->new(
                                expolygon        => $_,
                                surface_type     => $type,
                                thickness        => sum(map $_->height, @layerms),
                                thickness_layers => scalar(@layerms),
                            ),
                            @$intersection;
                    } else {
                        # save void surfaces
                        push @this_type,
                            map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNALVOID),
                            @{intersection_ex(
                                [ map @{$_->expolygon}, @this_type ],
                                [ @intersection_with_clearance ],
                            )};
                    }
                    
                    $layerm->fill_surfaces->clear;
                    $layerm->fill_surfaces->append(@new_this_type, @other_types);
                }
            }
        }
    }
}

sub _simplify_slices {
    my ($self, $distance) = @_;
    
    foreach my $layer (@{$self->layers}) {
        $layer->slices->simplify($distance);
        $_->slices->simplify($distance) for @{$layer->regions};
    }
}

sub support_material_flow {
    my ($self, $role) = @_;
    
    $role //= FLOW_ROLE_SUPPORT_MATERIAL;
    my $extruder = ($role == FLOW_ROLE_SUPPORT_MATERIAL)
        ? $self->config->support_material_extruder
        : $self->config->support_material_interface_extruder;
    
    # we use a bogus layer_height because we use the same flow for all
    # support material layers
    return Slic3r::Flow->new_from_width(
        width               => $self->config->support_material_extrusion_width || $self->config->extrusion_width,
        role                => $role,
        nozzle_diameter     => $self->print->config->nozzle_diameter->[$extruder-1] // $self->print->config->nozzle_diameter->[0],
        layer_height        => $self->config->layer_height,
        bridge_flow_ratio   => 0,
    );
}

1;
