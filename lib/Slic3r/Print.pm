package Slic3r::Print;
use Moo;

use File::Basename qw(basename fileparse);
use File::Spec;
use List::Util qw(max first);
use Math::ConvexHull::MonotoneChain qw(convex_hull);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(X Y Z X1 Y1 X2 Y2 MIN PI scale unscale move_points nearest_point);
use Slic3r::Geometry::Clipper qw(diff_ex union_ex intersection_ex offset JT_ROUND JT_SQUARE);
use Time::HiRes qw(gettimeofday tv_interval);

has 'config'                 => (is => 'rw', default => sub { Slic3r::Config->new_from_defaults }, trigger => 1);
has 'extra_variables'        => (is => 'rw', default => sub {{}});
has 'objects'                => (is => 'rw', default => sub {[]});
has 'total_extrusion_length' => (is => 'rw');
has 'processing_time'        => (is => 'rw');
has 'extruders'              => (is => 'rw', default => sub {[]});
has 'regions'                => (is => 'rw', default => sub {[]});
has 'support_material_flow'  => (is => 'rw');
has 'first_layer_support_material_flow' => (is => 'rw');

# ordered collection of extrusion paths to build skirt loops
has 'skirt' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::ExtrusionLoop]',
    default => sub { [] },
);

# ordered collection of extrusion paths to build a brim
has 'brim' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::ExtrusionLoop]',
    default => sub { [] },
);

sub BUILD {
    my $self = shift;
    
    # call this manually because the 'default' coderef doesn't trigger the trigger
    $self->_trigger_config;
}

sub _trigger_config {
    my $self = shift;
    
    # store config in a handy place
    $Slic3r::Config = $self->config;
    
    # legacy with existing config files
    $self->config->set('first_layer_height', $self->config->layer_height)
        if !$self->config->first_layer_height;
    $self->config->set_ifndef('small_perimeter_speed',  $self->config->perimeter_speed);
    $self->config->set_ifndef('bridge_speed',           $self->config->infill_speed);
    $self->config->set_ifndef('solid_infill_speed',     $self->config->infill_speed);
    $self->config->set_ifndef('top_solid_infill_speed', $self->config->solid_infill_speed);
    $self->config->set_ifndef('top_solid_layers',       $self->config->solid_layers);
    $self->config->set_ifndef('bottom_solid_layers',    $self->config->solid_layers);
    
    # G-code flavors
    $self->config->set('extrusion_axis', 'A') if $self->config->gcode_flavor eq 'mach3';
    $self->config->set('extrusion_axis', '')  if $self->config->gcode_flavor eq 'no-extrusion';
}

sub add_model {
    my $self = shift;
    my ($model) = @_;
    
    # append/merge materials and preserve a mapping between the original material ID
    # and our numeric material index
    my %materials = ();
    {
        my @material_ids = sort keys %{$model->materials};
        @material_ids = (0) if !@material_ids;
        for (my $i = $self->regions_count; $i < @material_ids; $i++) {
            push @{$self->regions}, Slic3r::Print::Region->new;
            $materials{$material_ids[$i]} = $#{$self->regions};
        }
    }
    
    foreach my $object (@{ $model->objects }) {
        my @meshes = ();  # by region_id
        
        foreach my $volume (@{$object->volumes}) {
            # should the object contain multiple volumes of the same material, merge them
            my $region_id = defined $volume->material_id ? $materials{$volume->material_id} : 0;
            my $mesh = $volume->mesh->clone;
            $meshes[$region_id] = $meshes[$region_id]
                ? Slic3r::TriangleMesh->merge($meshes[$region_id], $mesh)
                : $mesh;
        }
        
        foreach my $mesh (@meshes) {
            next unless $mesh;
            $mesh->check_manifoldness;
            
            if ($object->instances) {
                # we ignore the per-instance rotation currently and only 
                # consider the first one
                $mesh->rotate($object->instances->[0]->rotation);
            }
            
            $mesh->rotate($Slic3r::Config->rotate);
            $mesh->scale($Slic3r::Config->scale / &Slic3r::SCALING_FACTOR);
        }
        
        my $complete_mesh = Slic3r::TriangleMesh->merge(grep defined $_, @meshes);
        
        # initialize print object
        my $print_object = Slic3r::Print::Object->new(
            print       => $self,
            meshes      => [ @meshes ],
            size        => [ $complete_mesh->size ],
            input_file  => $object->input_file
        );
        push @{$self->objects}, $print_object;
        
        # align object to origin
        {
            my @extents = $complete_mesh->extents;
            foreach my $mesh (grep defined $_, @meshes) {
                $mesh->move(map -$extents[$_][MIN], X,Y,Z);
            }
        }
        
        if ($object->instances) {
            # replace the default [0,0] instance with the custom ones
            @{$print_object->copies} = map [ scale $_->offset->[X], scale $_->offset->[Y] ], @{$object->instances};
        }
    }
}

sub validate {
    my $self = shift;
    
    if ($Slic3r::Config->complete_objects) {
        # check horizontal clearance
        {
            my @a = ();
            for my $obj_idx (0 .. $#{$self->objects}) {
                my $clearance;
                {
                    my @points = map [ @$_[X,Y] ], map @{$_->vertices}, @{$self->objects->[$obj_idx]->meshes};
                    my $convex_hull = Slic3r::Polygon->new(convex_hull(\@points));
                    $clearance = +($convex_hull->offset(scale $Slic3r::Config->extruder_clearance_radius / 2, 1, JT_ROUND))[0];
                }
                for my $copy (@{$self->objects->[$obj_idx]->copies}) {
                    my $copy_clearance = $clearance->clone;
                    $copy_clearance->translate(@$copy);
                    if (@{ intersection_ex(\@a, [$copy_clearance]) }) {
                        die "Some objects are too close; your extruder will collide with them.\n";
                    }
                    @a = map @$_, @{union_ex([ @a, $copy_clearance ])};
                }
            }
        }
        
        # check vertical clearance
        {
            my @obj_copies = $self->object_copies;
            pop @obj_copies;  # ignore the last copy: its height doesn't matter
            my $scaled_clearance = scale $Slic3r::Config->extruder_clearance_height;
            if (grep { +($_->size)[Z] > $scaled_clearance } map @{$self->objects->[$_->[0]]->meshes}, @obj_copies) {
                die "Some objects are too tall and cannot be printed without extruder collisions.\n";
            }
        }
    }
}

sub init_extruders {
    my $self = shift;
    
    # map regions to extruders (ghetto mapping for now)
    my %extruder_mapping = map { $_ => $_ } 0..$#{$self->regions};
    
    # initialize all extruder(s) we need
    my @used_extruders = (
        0,
        (map $self->config->get("${_}_extruder")-1, qw(perimeter infill support_material)),
        (values %extruder_mapping),
    );
    for my $extruder_id (keys %{{ map {$_ => 1} @used_extruders }}) {
        $self->extruders->[$extruder_id] = Slic3r::Extruder->new(
            id => $extruder_id,
            map { $_ => $self->config->get($_)->[$extruder_id] // $self->config->get($_)->[0] } #/
                @{&Slic3r::Extruder::OPTIONS}
        );
    }
    
    # calculate default flows
    $Slic3r::flow = $self->extruders->[0]->make_flow(
        width           => $self->config->extrusion_width,
    );
    $Slic3r::first_layer_flow = $self->extruders->[0]->make_flow(
        layer_height    => $self->config->get_value('first_layer_height'),
        width           => $self->config->first_layer_extrusion_width,
    );
    
    # calculate regions' flows
    for my $region_id (0 .. $#{$self->regions}) {
        my $region = $self->regions->[$region_id];
        
        # per-role extruders and flows
        for (qw(perimeter infill)) {
            $region->extruders->{$_} = ($self->regions_count > 1)
                ? $self->extruders->[$extruder_mapping{$region_id}]
                : $self->extruders->[$self->config->get("${_}_extruder")-1];
            $region->flows->{$_} = $region->extruders->{$_}->make_flow(
                width => $self->config->get("${_}_extrusion_width") || $self->config->extrusion_width,
            );
            $region->first_layer_flows->{$_} = $region->extruders->{$_}->make_flow(
                layer_height    => $self->config->get_value('first_layer_height'),
                width           => $self->config->first_layer_extrusion_width,
            );
        }
    }
    
    # calculate support material flow
    if ($self->config->support_material) {
        my $extruder = $self->extruders->[$self->config->support_material_extruder-1];
        $self->support_material_flow($extruder->make_flow(
            width => $self->config->support_material_extrusion_width || $self->config->extrusion_width,
        ));
        $self->first_layer_support_material_flow($extruder->make_flow(
            layer_height    => $self->config->get_value('first_layer_height'),
            width           => $self->config->first_layer_extrusion_width,
        ));
    }
    
    Slic3r::debugf "Default flow width = %s (spacing = %s)\n",
        $Slic3r::flow->width, $Slic3r::flow->spacing;
}

sub object_copies {
    my $self = shift;
    my @oc = ();
    for my $obj_idx (0 .. $#{$self->objects}) {
        push @oc, map [ $obj_idx, $_ ], @{$self->objects->[$obj_idx]->copies};
    }
    return @oc;
}

sub layer_count {
    my $self = shift;
    my $count = 0;
    foreach my $object (@{$self->objects}) {
        $count = @{$object->layers} if @{$object->layers} > $count;
    }
    return $count;
}

sub regions_count {
    my $self = shift;
    return scalar @{$self->regions};
}

sub duplicate {
    my $self = shift;
    
    if ($Slic3r::Config->duplicate_grid->[X] > 1 || $Slic3r::Config->duplicate_grid->[Y] > 1) {
        if (@{$self->objects} > 1) {
            die "Grid duplication is not supported with multiple objects\n";
        }
        my $object = $self->objects->[0];
        
        # generate offsets for copies
        my $dist = scale $Slic3r::Config->duplicate_distance;
        @{$self->objects->[0]->copies} = ();
        for my $x_copy (1..$Slic3r::Config->duplicate_grid->[X]) {
            for my $y_copy (1..$Slic3r::Config->duplicate_grid->[Y]) {
                push @{$self->objects->[0]->copies}, [
                    ($object->size->[X] + $dist) * ($x_copy-1),
                    ($object->size->[Y] + $dist) * ($y_copy-1),
                ];
            }
        }
    } elsif ($Slic3r::Config->duplicate > 1) {
        foreach my $object (@{$self->objects}) {
            @{$object->copies} = map [0,0], 1..$Slic3r::Config->duplicate;
        }
        $self->arrange_objects;
    }
}

sub arrange_objects {
    my $self = shift;

    my $total_parts = scalar map @{$_->copies}, @{$self->objects};
    my $partx = max(map $_->size->[X], @{$self->objects});
    my $party = max(map $_->size->[Y], @{$self->objects});
    
    my @positions = Slic3r::Geometry::arrange
        ($total_parts, $partx, $party, (map scale $_, @{$Slic3r::Config->bed_size}), scale $Slic3r::Config->min_object_distance, $self->config);
    
    @{$_->copies} = splice @positions, 0, scalar @{$_->copies} for @{$self->objects};
}

sub bounding_box {
    my $self = shift;
    
    my @points = ();
    foreach my $obj_idx (0 .. $#{$self->objects}) {
        my $object = $self->objects->[$obj_idx];
        foreach my $copy (@{$self->objects->[$obj_idx]->copies}) {
            push @points,
                [ $copy->[X], $copy->[Y] ],
                [ $copy->[X] + $object->size->[X], $copy->[Y] ],
                [ $copy->[X] + $object->size->[X], $copy->[Y] + $object->size->[Y] ],
                [ $copy->[X], $copy->[Y] + $object->size->[Y] ];
        }
    }
    return Slic3r::Geometry::bounding_box(\@points);
}

sub size {
    my $self = shift;
    
    my @bb = $self->bounding_box;
    return [ $bb[X2] - $bb[X1], $bb[Y2] - $bb[Y1] ];
}

sub export_gcode {
    my $self = shift;
    my %params = @_;
    
    $self->init_extruders;
    my $status_cb = $params{status_cb} || sub {};
    my $t0 = [gettimeofday];
    
    # skein the STL into layers
    # each layer has surfaces with holes
    $status_cb->(10, "Processing triangulated mesh");
    $_->slice(keep_meshes => $params{keep_meshes}) for @{$self->objects};
    
    # make perimeters
    # this will add a set of extrusion loops to each layer
    # as well as generate infill boundaries
    $status_cb->(20, "Generating perimeters");
    $_->make_perimeters for @{$self->objects};
    
    # simplify slices (both layer and region slices),
    # we only need the max resolution for perimeters
    foreach my $layer (map @{$_->layers}, @{$self->objects}) {
        $_->simplify(&Slic3r::SCALED_RESOLUTION)
            for @{$layer->slices}, (map $_->expolygon, map @{$_->slices}, @{$layer->regions});
    }
    
    # this will transform $layer->fill_surfaces from expolygon 
    # to typed top/bottom/internal surfaces;
    $status_cb->(30, "Detecting solid surfaces");
    $_->detect_surfaces_type for @{$self->objects};
    
    # decide what surfaces are to be filled
    $status_cb->(35, "Preparing infill surfaces");
    $_->prepare_fill_surfaces for map @{$_->regions}, map @{$_->layers}, @{$self->objects};
    
    # this will detect bridges and reverse bridges
    # and rearrange top/bottom/internal surfaces
    $status_cb->(45, "Detect bridges");
    $_->process_bridges for map @{$_->regions}, map @{$_->layers}, @{$self->objects};
    
    # detect which fill surfaces are near external layers
    # they will be split in internal and internal-solid surfaces
    $status_cb->(60, "Generating horizontal shells");
    $_->discover_horizontal_shells for @{$self->objects};
    
    # combine fill surfaces to honor the "infill every N layers" option
    $status_cb->(70, "Combining infill");
    $_->combine_infill for @{$self->objects};
    
    # this will generate extrusion paths for each layer
    $status_cb->(80, "Infilling layers");
    {
        my $fill_maker = Slic3r::Fill->new('print' => $self);
        Slic3r::parallelize(
            items => sub {
                my @items = ();  # [obj_idx, layer_id]
                for my $obj_idx (0 .. $#{$self->objects}) {
                    for my $region_id (0 .. ($self->regions_count-1)) {
                        push @items, map [$obj_idx, $_, $region_id], 0..($self->objects->[$obj_idx]->layer_count-1);
                    }
                }
                @items;
            },
            thread_cb => sub {
                my $q = shift;
                $Slic3r::Geometry::Clipper::clipper = Math::Clipper->new;
                my $fills = {};
                while (defined (my $obj_layer = $q->dequeue)) {
                    my ($obj_idx, $layer_id, $region_id) = @$obj_layer;
                    $fills->{$obj_idx} ||= {};
                    $fills->{$obj_idx}{$layer_id} ||= {};
                    $fills->{$obj_idx}{$layer_id}{$region_id} = [
                        $fill_maker->make_fill($self->objects->[$obj_idx]->layers->[$layer_id]->regions->[$region_id]),
                    ];
                }
                return $fills;
            },
            collect_cb => sub {
                my $fills = shift;
                foreach my $obj_idx (keys %$fills) {
                    my $object = $self->objects->[$obj_idx];
                    foreach my $layer_id (keys %{$fills->{$obj_idx}}) {
                        my $layer = $object->layers->[$layer_id];
                        foreach my $region_id (keys %{$fills->{$obj_idx}{$layer_id}}) {
                            $layer->regions->[$region_id]->fills($fills->{$obj_idx}{$layer_id}{$region_id});
                        }
                    }
                }
            },
            no_threads_cb => sub {
                foreach my $layerm (map @{$_->regions}, map @{$_->layers}, @{$self->objects}) {
                    $layerm->fills([ $fill_maker->make_fill($layerm) ]);
                }
            },
        );
    }
    
    # generate support material
    if ($Slic3r::Config->support_material) {
        $status_cb->(85, "Generating support material");
        $_->generate_support_material for @{$self->objects};
    }
    
    # free memory (note that support material needs fill_surfaces)
    $_->fill_surfaces(undef) for map @{$_->regions}, map @{$_->layers}, @{$self->objects};
    
    # make skirt
    $status_cb->(88, "Generating skirt");
    $self->make_skirt;
    $self->make_brim;  # must come after make_skirt
    
    # output everything to a G-code file
    my $output_file = $self->expanded_output_filepath($params{output_file});
    $status_cb->(90, "Exporting G-code" . ($output_file ? " to $output_file" : ""));
    $self->write_gcode($params{output_fh} || $output_file);
    
    # run post-processing scripts
    if (@{$Slic3r::Config->post_process}) {
        $status_cb->(95, "Running post-processing scripts");
        $Slic3r::Config->setenv;
        for (@{$Slic3r::Config->post_process}) {
            Slic3r::debugf "  '%s' '%s'\n", $_, $output_file;
            system($_, $output_file);
        }
    }
    
    # output some statistics
    unless ($params{quiet}) {
        $self->processing_time(tv_interval($t0));
        printf "Done. Process took %d minutes and %.3f seconds\n", 
            int($self->processing_time/60),
            $self->processing_time - int($self->processing_time/60)*60;
        
        # TODO: more statistics!
        printf "Filament required: %.1fmm (%.1fcm3)\n",
            $self->total_extrusion_length, $self->total_extrusion_volume;
    }
}

sub export_svg {
    my $self = shift;
    my %params = @_;
    
    # this shouldn't be needed, but we're currently relying on ->make_surfaces() which
    # calls ->perimeter_flow
    $self->init_extruders;
    
    $_->slice(keep_meshes => $params{keep_meshes}) for @{$self->objects};
    $self->arrange_objects;
    
    my $output_file = $self->expanded_output_filepath($params{output_file});
    $output_file =~ s/\.gcode$/.svg/i;
    
    open my $fh, ">", $output_file or die "Failed to open $output_file for writing\n";
    print "Exporting to $output_file...";
    my $print_size = $self->size;
    print $fh sprintf <<"EOF", unscale($print_size->[X]), unscale($print_size->[Y]);
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.0//EN" "http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd">
<svg width="%s" height="%s" xmlns="http://www.w3.org/2000/svg" xmlns:svg="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:slic3r="http://slic3r.org/namespaces/slic3r">
  <!-- 
  Generated using Slic3r $Slic3r::VERSION
  http://slic3r.org/
   -->
EOF
    
    my $print_polygon = sub {
        my ($polygon, $type) = @_;
        printf $fh qq{    <polygon slic3r:type="%s" points="%s" style="fill: %s" />\n},
            $type, (join ' ', map { join ',', map unscale $_, @$_ } @$polygon),
            ($type eq 'contour' ? 'white' : 'black');
    };
    
    my @previous_layer_slices = ();
    for my $layer_id (0..$self->layer_count-1) {
        my @layers = map $_->layers->[$layer_id], @{$self->objects};
        printf $fh qq{  <g id="layer%d" slic3r:z="%s">\n}, $layer_id, unscale +(grep defined $_, @layers)[0]->slice_z;
        
        my @current_layer_slices = ();
        for my $obj_idx (0 .. $#{$self->objects}) {
            my $layer = $self->objects->[$obj_idx]->layers->[$layer_id] or next;
            
            # sort slices so that the outermost ones come first
            my @slices = sort { $a->contour->encloses_point($b->contour->[0]) ? 0 : 1 } @{$layer->slices};
            foreach my $copy (@{$self->objects->[$obj_idx]->copies}) {
                foreach my $slice (@slices) {
                    my $expolygon = $slice->clone;
                    $expolygon->translate(@$copy);
                    $print_polygon->($expolygon->contour, 'contour');
                    $print_polygon->($_, 'hole') for $expolygon->holes;
                    push @current_layer_slices, $expolygon;
                }
            }
        }
        # generate support material
        if ($Slic3r::Config->support_material && $layer_id > 0) {
            my (@supported_slices, @unsupported_slices) = ();
            foreach my $expolygon (@current_layer_slices) {
                my $intersection = intersection_ex(
                    [ map @$_, @previous_layer_slices ],
                    $expolygon,
                );
                @$intersection
                    ? push @supported_slices, $expolygon
                    : push @unsupported_slices, $expolygon;
            }
            my @supported_points = map @$_, @$_, @supported_slices;
            foreach my $expolygon (@unsupported_slices) {
                # look for the nearest point to this island among all
                # supported points
                my $support_point = nearest_point($expolygon->contour->[0], \@supported_points)
                    or next;
                my $anchor_point = nearest_point($support_point, $expolygon->contour);
                printf $fh qq{    <line x1="%s" y1="%s" x2="%s" y2="%s" style="stroke-width: 2; stroke: white" />\n},
                    map @$_, $support_point, $anchor_point;
            }
        }
        print $fh qq{  </g>\n};
        @previous_layer_slices = @current_layer_slices;
    }
    
    print $fh "</svg>\n";
    close $fh;
    print "Done.\n";
}

sub make_skirt {
    my $self = shift;
    return unless $Slic3r::Config->skirts > 0;
    
    # collect points from all layers contained in skirt height
    my $skirt_height = $Slic3r::Config->skirt_height;
    $skirt_height = $self->layer_count if $skirt_height > $self->layer_count;
    my @points = ();
    foreach my $obj_idx (0 .. $#{$self->objects}) {
        my @layers = map $self->objects->[$obj_idx]->layers->[$_], 0..($skirt_height-1);
        my @layer_points = (
            (map @$_, map @$_, map @{$_->slices}, @layers),
            (map @$_, map @{$_->thin_walls}, map @{$_->regions}, @layers),
            (map @{$_->unpack->polyline}, map @{$_->support_fills->paths}, grep $_->support_fills, @layers),
        );
        push @points, map move_points($_, @layer_points), @{$self->objects->[$obj_idx]->copies};
    }
    return if @points < 3;  # at least three points required for a convex hull
    
    # find out convex hull
    my $convex_hull = convex_hull(\@points);
    
    my @extruded_length = ();  # for each extruder
    my $spacing = $Slic3r::first_layer_flow->spacing;
    my $first_layer_height = $Slic3r::Config->get_value('first_layer_height');
    my @extruders_e_per_mm = ();
    my $extruder_idx = 0;
    
    # draw outlines from outside to inside
    # loop while we have less skirts than required or any extruder hasn't reached the min length if any
    my $distance = scale $Slic3r::Config->skirt_distance;
    for (my $i = $Slic3r::Config->skirts; $i > 0; $i--) {
        $distance += scale $spacing;
        my ($loop) = Slic3r::Geometry::Clipper::offset([$convex_hull], $distance, 0.0001, JT_ROUND);
        push @{$self->skirt}, Slic3r::ExtrusionLoop->pack(
            polygon         => Slic3r::Polygon->new(@$loop),
            role            => EXTR_ROLE_SKIRT,
            flow_spacing    => $spacing,
        );
        
        if ($Slic3r::Config->min_skirt_length > 0) {
            bless $loop, 'Slic3r::Polygon';
            $extruded_length[$extruder_idx]     ||= 0;
            $extruders_e_per_mm[$extruder_idx]  ||= $self->extruders->[$extruder_idx]->e_per_mm($spacing, $first_layer_height);
            $extruded_length[$extruder_idx]     += unscale $loop->length * $extruders_e_per_mm[$extruder_idx];
            $i++ if defined first { ($extruded_length[$_] // 0) < $Slic3r::Config->min_skirt_length } 0 .. $#{$self->extruders};
            if ($extruded_length[$extruder_idx] >= $Slic3r::Config->min_skirt_length) {
                if ($extruder_idx < $#{$self->extruders}) {
                    $extruder_idx++;
                    next;
                }
            }
        }
    }
    
    @{$self->skirt} = reverse @{$self->skirt};
}

sub make_brim {
    my $self = shift;
    return unless $Slic3r::Config->brim_width > 0;
    
    my $grow_distance = $Slic3r::first_layer_flow->scaled_width / 2;
    my @islands = (); # array of polygons
    foreach my $obj_idx (0 .. $#{$self->objects}) {
        my $layer0 = $self->objects->[$obj_idx]->layers->[0];
        my @object_islands = (
            (map $_->contour, @{$layer0->slices}),
            (map { $_->isa('Slic3r::Polygon') ? $_ : $_->grow($grow_distance) } map @{$_->thin_walls}, @{$layer0->regions}),
            (map $_->unpack->polyline->grow($grow_distance), map @{$_->support_fills->paths}, grep $_->support_fills, $layer0),
        );
        foreach my $copy (@{$self->objects->[$obj_idx]->copies}) {
            push @islands, map $_->clone->translate(@$copy), @object_islands;
        }
    }
    
    # if brim touches skirt, make it around skirt too
    if ($Slic3r::Config->skirt_distance + (($Slic3r::Config->skirts - 1) * $Slic3r::first_layer_flow->spacing) <= $Slic3r::Config->brim_width) {
        push @islands, map $_->unpack->split_at_first_point->polyline->grow($grow_distance), @{$self->skirt};
    }
    
    my $num_loops = sprintf "%.0f", $Slic3r::Config->brim_width / $Slic3r::first_layer_flow->width;
    for my $i (reverse 1 .. $num_loops) {
        # JT_SQUARE ensures no vertex is outside the given offset distance
        push @{$self->brim}, Slic3r::ExtrusionLoop->pack(
            polygon         => Slic3r::Polygon->new($_),
            role            => EXTR_ROLE_SKIRT,
            flow_spacing    => $Slic3r::first_layer_flow->spacing,
        ) for Slic3r::Geometry::Clipper::offset(\@islands, $i * $Slic3r::first_layer_flow->scaled_spacing, undef, JT_SQUARE);
        # TODO: we need the offset inwards/offset outwards logic to avoid overlapping extrusions
    }
}

sub write_gcode {
    my $self = shift;
    my ($file) = @_;
    
    # open output gcode file if we weren't supplied a file-handle
    my $fh;
    if (ref $file eq 'IO::Scalar') {
        $fh = $file;
    } else {
        open $fh, ">", $file
            or die "Failed to open $file for writing\n";
    }
    
    # write some information
    my @lt = localtime;
    printf $fh "; generated by Slic3r $Slic3r::VERSION on %04d-%02d-%02d at %02d:%02d:%02d\n\n",
        $lt[5] + 1900, $lt[4]+1, $lt[3], $lt[2], $lt[1], $lt[0];

    print $fh "; $_\n" foreach split /\R/, $Slic3r::Config->notes;
    print $fh "\n" if $Slic3r::Config->notes;
    
    for (qw(layer_height perimeters top_solid_layers bottom_solid_layers fill_density perimeter_speed infill_speed travel_speed scale)) {
        printf $fh "; %s = %s\n", $_, $Slic3r::Config->$_;
    }
    for (qw(nozzle_diameter filament_diameter extrusion_multiplier)) {
        printf $fh "; %s = %s\n", $_, $Slic3r::Config->$_->[0];
    }
    printf $fh "; perimeters extrusion width = %.2fmm\n", $self->regions->[0]->flows->{perimeter}->width;
    printf $fh "; infill extrusion width = %.2fmm\n", $self->regions->[0]->flows->{infill}->width;
    printf $fh "; support material extrusion width = %.2fmm\n", $self->support_material_flow->width
        if $self->support_material_flow;
    printf $fh "; first layer extrusion width = %.2fmm\n", $Slic3r::first_layer_flow->width
        if $Slic3r::first_layer_flow;
    print  $fh "\n";
    
    # set up our extruder object
    my $gcodegen = Slic3r::GCode->new(
        multiple_extruders => (@{$self->extruders} > 1),
    );
    my $min_print_speed = 60 * $Slic3r::Config->min_print_speed;
    my $dec = $gcodegen->dec;
    print $fh $gcodegen->set_fan(0, 1) if $Slic3r::Config->cooling && $Slic3r::Config->disable_fan_first_layers;
    
    # write start commands to file
    printf $fh $gcodegen->set_bed_temperature($Slic3r::Config->first_layer_bed_temperature, 1),
        if $Slic3r::Config->first_layer_bed_temperature && $Slic3r::Config->start_gcode !~ /M190/i;
    my $print_first_layer_temperature = sub {
        for my $t (grep $self->extruders->[$_], 0 .. $#{$Slic3r::Config->first_layer_temperature}) {
            printf $fh $gcodegen->set_temperature($self->extruders->[$t]->first_layer_temperature, 0, $t)
                if $self->extruders->[$t]->first_layer_temperature;
        }
    };
    $print_first_layer_temperature->();
    printf $fh "%s\n", $Slic3r::Config->replace_options($Slic3r::Config->start_gcode);
    for my $t (grep $self->extruders->[$_], 0 .. $#{$Slic3r::Config->first_layer_temperature}) {
        printf $fh $gcodegen->set_temperature($self->extruders->[$t]->first_layer_temperature, 1, $t)
            if $self->extruders->[$t]->first_layer_temperature && $Slic3r::Config->start_gcode !~ /M109/i;
    }
    print  $fh "G90 ; use absolute coordinates\n";
    print  $fh "G21 ; set units to millimeters\n";
    if ($Slic3r::Config->gcode_flavor =~ /^(?:reprap|teacup)$/) {
        printf $fh $gcodegen->reset_e;
        if ($Slic3r::Config->gcode_flavor =~ /^(?:reprap|makerbot)$/) {
            if ($Slic3r::Config->use_relative_e_distances) {
                print $fh "M83 ; use relative distances for extrusion\n";
            } else {
                print $fh "M82 ; use absolute distances for extrusion\n";
            }
        }
    }
    
    # calculate X,Y shift to center print around specified origin
    my @print_bb = $self->bounding_box;
    my @shift = (
        $Slic3r::Config->print_center->[X] - (unscale ($print_bb[X2] - $print_bb[X1]) / 2) - unscale $print_bb[X1],
        $Slic3r::Config->print_center->[Y] - (unscale ($print_bb[Y2] - $print_bb[Y1]) / 2) - unscale $print_bb[Y1],
    );
    
    # prepare the logic to print one layer
    my $skirt_done = 0;  # count of skirt layers done
    my $brim_done = 0;
    my $extrude_layer = sub {
        my ($layer_id, $object_copies) = @_;
        my $gcode = "";
        
        if ($layer_id == 1) {
            for my $t (grep $self->extruders->[$_], 0 .. $#{$Slic3r::Config->temperature}) {
                $gcode .= $gcodegen->set_temperature($self->extruders->[$t]->temperature, 0, $t)
                    if $self->extruders->[$t]->temperature && $self->extruders->[$t]->temperature != $self->extruders->[$t]->first_layer_temperature;
            }
            $gcode .= $gcodegen->set_bed_temperature($Slic3r::Config->bed_temperature)
                if $Slic3r::Config->bed_temperature && $Slic3r::Config->bed_temperature != $Slic3r::Config->first_layer_bed_temperature;
        }
        
        # set new layer, but don't move Z as support material interfaces may need an intermediate one
        $gcodegen->layer($self->objects->[$object_copies->[0][0]]->layers->[$layer_id]);
        $gcodegen->elapsed_time(0);
        
        # prepare callback to call as soon as a Z command is generated
        $gcodegen->move_z_callback(sub {
            $gcodegen->move_z_callback(undef);  # circular ref or not?
            return "" if !$Slic3r::Config->layer_gcode;
            return $Slic3r::Config->replace_options($Slic3r::Config->layer_gcode) . "\n";
        });
        
        # extrude skirt
        if ($skirt_done < $Slic3r::Config->skirt_height) {
            $gcodegen->set_shift(@shift);
            $gcode .= $gcodegen->set_extruder($self->extruders->[0]);  # move_z requires extruder
            $gcode .= $gcodegen->move_z($gcodegen->layer->print_z);
            # skip skirt if we have a large brim
            if ($layer_id < $Slic3r::Config->skirt_height) {
                # distribute skirt loops across all extruders
                for my $i (0 .. $#{$self->skirt}) {
                    # when printing layers > 0 ignore 'min_skirt_length' and 
                    # just use the 'skirts' setting; also just use the current extruder
                    last if ($layer_id > 0) && ($i >= $Slic3r::Config->skirts);
                    $gcode .= $gcodegen->set_extruder($self->extruders->[ ($i/@{$self->extruders}) % @{$self->extruders} ])
                        if $layer_id == 0;
                    $gcode .= $gcodegen->extrude_loop($self->skirt->[$i], 'skirt');
                }
            }
            $skirt_done++;
        }
        
        # extrude brim
        if ($layer_id == 0 && !$brim_done) {
            $gcode .= $gcodegen->set_extruder($self->extruders->[$Slic3r::Config->support_material_extruder-1]);  # move_z requires extruder
            $gcode .= $gcodegen->move_z($gcodegen->layer->print_z);
            $gcodegen->set_shift(@shift);
            $gcode .= $gcodegen->extrude_loop($_, 'brim') for @{$self->brim};
            $brim_done = 1;
        }
        
        for my $obj_copy (@$object_copies) {
            my ($obj_idx, $copy) = @$obj_copy;
            my $layer = $self->objects->[$obj_idx]->layers->[$layer_id];
            
            $gcodegen->set_shift(map $shift[$_] + unscale $copy->[$_], X,Y);
            
            # extrude support material before other things because it might use a lower Z
            # and also because we avoid travelling on other things when printing it
            if ($Slic3r::Config->support_material) {
                $gcode .= $gcodegen->move_z($layer->support_material_interface_z)
                    if ($layer->support_interface_fills && @{ $layer->support_interface_fills->paths });
                $gcode .= $gcodegen->set_extruder($self->extruders->[$Slic3r::Config->support_material_extruder-1]);
                if ($layer->support_interface_fills) {
                    $gcode .= $gcodegen->extrude_path($_, 'support material interface') 
                        for $layer->support_interface_fills->shortest_path($gcodegen->last_pos); 
                }
                
                $gcode .= $gcodegen->move_z($layer->print_z);
                if ($layer->support_fills) {
                    $gcode .= $gcodegen->extrude_path($_, 'support material') 
                        for $layer->support_fills->shortest_path($gcodegen->last_pos);
                }
            }
            
            # set actual Z - this will force a retraction
            $gcode .= $gcodegen->move_z($layer->print_z);
            
            foreach my $region_id (0 .. ($self->regions_count-1)) {
                my $layerm = $layer->regions->[$region_id];
                my $region = $self->regions->[$region_id];
                
                # extrude perimeters
                if (@{ $layerm->perimeters }) {
                    $gcode .= $gcodegen->set_extruder($region->extruders->{perimeter});
                    $gcode .= $gcodegen->set_acceleration($Slic3r::Config->perimeter_acceleration);
                    $gcode .= $gcodegen->extrude($_, 'perimeter') for @{ $layerm->perimeters };
                    $gcode .= $gcodegen->set_acceleration($Slic3r::Config->default_acceleration)
                        if $Slic3r::Config->perimeter_acceleration;
                }
                
                # extrude fills
                if (@{ $layerm->fills }) {
                    $gcode .= $gcodegen->set_extruder($region->extruders->{infill});
                    $gcode .= $gcodegen->set_acceleration($Slic3r::Config->infill_acceleration);
                    for my $fill (@{ $layerm->fills }) {
                        if ($fill->isa('Slic3r::ExtrusionPath::Collection')) {
                            $gcode .= $gcodegen->extrude($_, 'fill') 
                                for $fill->shortest_path($gcodegen->last_pos);
                        } else {
                            $gcode .= $gcodegen->extrude($fill, 'fill') ;
                        }
                    }
                    $gcode .= $gcodegen->set_acceleration($Slic3r::Config->default_acceleration)
                        if $Slic3r::Config->infill_acceleration;
                }
            }
        }
        return if !$gcode;
        
        my $fan_speed = $Slic3r::Config->fan_always_on ? $Slic3r::Config->min_fan_speed : 0;
        my $speed_factor = 1;
        if ($Slic3r::Config->cooling) {
            my $layer_time = $gcodegen->elapsed_time;
            Slic3r::debugf "Layer %d estimated printing time: %d seconds\n", $layer_id, $layer_time;
            if ($layer_time < $Slic3r::Config->slowdown_below_layer_time) {
                $fan_speed = $Slic3r::Config->max_fan_speed;
                $speed_factor = $layer_time / $Slic3r::Config->slowdown_below_layer_time;
            } elsif ($layer_time < $Slic3r::Config->fan_below_layer_time) {
                $fan_speed = $Slic3r::Config->max_fan_speed - ($Slic3r::Config->max_fan_speed - $Slic3r::Config->min_fan_speed)
                    * ($layer_time - $Slic3r::Config->slowdown_below_layer_time)
                    / ($Slic3r::Config->fan_below_layer_time - $Slic3r::Config->slowdown_below_layer_time); #/
            }
            Slic3r::debugf "  fan = %d%%, speed = %d%%\n", $fan_speed, $speed_factor * 100;
            
            if ($speed_factor < 1) {
                $gcode =~ s/^(?=.*? [XY])(?=.*? E)(?<!;_BRIDGE_FAN_START\n)(G1 .*?F)(\d+(?:\.\d+)?)/
                    my $new_speed = $2 * $speed_factor;
                    $1 . sprintf("%.${dec}f", $new_speed < $min_print_speed ? $min_print_speed : $new_speed)
                    /gexm;
            }
            $fan_speed = 0 if $layer_id < $Slic3r::Config->disable_fan_first_layers;
        }
        $gcode = $gcodegen->set_fan($fan_speed) . $gcode;
        
        # bridge fan speed
        if (!$Slic3r::Config->cooling || $Slic3r::Config->bridge_fan_speed == 0 || $layer_id < $Slic3r::Config->disable_fan_first_layers) {
            $gcode =~ s/^;_BRIDGE_FAN_(?:START|END)\n//gm;
        } else {
            $gcode =~ s/^;_BRIDGE_FAN_START\n/ $gcodegen->set_fan($Slic3r::Config->bridge_fan_speed, 1) /gmex;
            $gcode =~ s/^;_BRIDGE_FAN_END\n/ $gcodegen->set_fan($fan_speed, 1) /gmex;
        }
        
        return $gcode;
    };
    
    # do all objects for each layer
    if ($Slic3r::Config->complete_objects) {
        
        # print objects from the smallest to the tallest to avoid collisions
        # when moving onto next object starting point
        my @obj_idx = sort { $self->objects->[$a]->layer_count <=> $self->objects->[$b]->layer_count } 0..$#{$self->objects};
        
        my $finished_objects = 0;
        for my $obj_idx (@obj_idx) {
            for my $copy (@{ $self->objects->[$obj_idx]->copies }) {
                # move to the origin position for the copy we're going to print.
                # this happens before Z goes down to layer 0 again, so that 
                # no collision happens hopefully.
                if ($finished_objects > 0) {
                    $gcodegen->set_shift(map $shift[$_] + unscale $copy->[$_], X,Y);
                    print $fh $gcodegen->retract;
                    print $fh $gcodegen->G0(Slic3r::Point->new(0,0), undef, 0, 'move to origin position for next object');
                }
                
                for my $layer_id (0..$#{$self->objects->[$obj_idx]->layers}) {
                    # if we are printing the bottom layer of an object, and we have already finished
                    # another one, set first layer temperatures. this happens before the Z move
                    # is triggered, so machine has more time to reach such temperatures
                    if ($layer_id == 0 && $finished_objects > 0) {
                        printf $fh $gcodegen->set_bed_temperature($Slic3r::Config->first_layer_bed_temperature),
                            if $Slic3r::Config->first_layer_bed_temperature;
                        $print_first_layer_temperature->();
                    }
                    print $fh $extrude_layer->($layer_id, [[ $obj_idx, $copy ]]);
                }
                $finished_objects++;
            }
        }
    } else {
        for my $layer_id (0..$self->layer_count-1) {
            my @object_copies = ();
            for my $obj_idx (grep $self->objects->[$_]->layers->[$layer_id], 0..$#{$self->objects}) {
                push @object_copies, map [ $obj_idx, $_ ], @{ $self->objects->[$obj_idx]->copies };
            }
            print $fh $extrude_layer->($layer_id, \@object_copies);
        }
    }
    
    # save statistic data
    $self->total_extrusion_length($gcodegen->total_extrusion_length);
    
    # write end commands to file
    print $fh $gcodegen->retract;
    print $fh $gcodegen->set_fan(0);
    printf $fh "%s\n", $Slic3r::Config->replace_options($Slic3r::Config->end_gcode);
    
    printf $fh "; filament used = %.1fmm (%.1fcm3)\n",
        $self->total_extrusion_length, $self->total_extrusion_volume;
    
    if ($Slic3r::Config->gcode_comments) {
        # append full config
        print $fh "\n";
        foreach my $opt_key (sort keys %{$Slic3r::Config}) {
            next if $Slic3r::Config::Options->{$opt_key}{shortcut};
            next if $Slic3r::Config::Options->{$opt_key}{gui_only};
            printf $fh "; %s = %s\n", $opt_key, $Slic3r::Config->serialize($opt_key);
        }
    }
    
    # close our gcode file
    close $fh;
}

sub total_extrusion_volume {
    my $self = shift;
    return $self->total_extrusion_length * ($self->extruders->[0]->filament_diameter**2) * PI/4 / 1000;
}

# this method will return the supplied input file path after expanding its
# format variables with their values
sub expanded_output_filepath {
    my $self = shift;
    my ($path, $input_file) = @_;
    
    # if no input file was supplied, take the first one from our objects
    $input_file ||= $self->objects->[0]->input_file;
    return undef if !defined $input_file;
    
    # if output path is an existing directory, we take that and append
    # the specified filename format
    $path = File::Spec->join($path, $Slic3r::Config->output_filename_format) if ($path && -d $path);

    # if no explicit output file was defined, we take the input
    # file directory and append the specified filename format
    $path ||= (fileparse($input_file))[1] . $Slic3r::Config->output_filename_format;
    
    my $input_filename = my $input_filename_base = basename($input_file);
    $input_filename_base =~ s/\.(?:stl|amf(?:\.xml)?)$//i;
    
    return $Slic3r::Config->replace_options($path, {
        input_filename      => $input_filename,
        input_filename_base => $input_filename_base,
        %{ $self->extra_variables },
    });
}

1;
