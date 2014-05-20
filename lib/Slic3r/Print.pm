package Slic3r::Print;
use Moo;

use File::Basename qw(basename fileparse);
use File::Spec;
use List::Util qw(min max first);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(X Y Z X1 Y1 X2 Y2 MIN MAX PI scale unscale move_points chained_path
    convex_hull);
use Slic3r::Geometry::Clipper qw(diff_ex union_ex union_pt intersection_ex intersection offset
    offset2 union union_pt_chained JT_ROUND JT_SQUARE);
use Slic3r::Print::State ':steps';

has 'config'                 => (is => 'ro', default => sub { Slic3r::Config::Print->new });
has 'default_object_config'  => (is => 'ro', default => sub { Slic3r::Config::PrintObject->new });
has 'default_region_config'  => (is => 'ro', default => sub { Slic3r::Config::PrintRegion->new });
has 'placeholder_parser'     => (is => 'rw', default => sub { Slic3r::GCode::PlaceholderParser->new });
has 'objects'                => (is => 'rw', default => sub {[]});
has 'status_cb'              => (is => 'rw');
has 'regions'                => (is => 'rw', default => sub {[]});
has 'total_used_filament'    => (is => 'rw');
has 'total_extruded_volume'  => (is => 'rw');
has '_state'                 => (is => 'ro', default => sub { Slic3r::Print::State->new });

# ordered collection of extrusion paths to build skirt loops
has 'skirt' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

# ordered collection of extrusion paths to build a brim
has 'brim' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

sub apply_config {
    my ($self, $config) = @_;
    
    $config = $config->clone;
    $config->normalize;
    
    # apply variables to placeholder parser
    $self->placeholder_parser->apply_config($config);
    
    # handle changes to print config
    my $print_diff = $self->config->diff($config);
    if (@$print_diff) {
        $self->config->apply_dynamic($config);
        
        # TODO: only invalidate changed steps
        $self->_state->invalidate_all;
    }
    
    # handle changes to object config defaults
    $self->default_object_config->apply_dynamic($config);
    foreach my $object (@{$self->objects}) {
        # we don't assume that $config contains a full ObjectConfig,
        # so we base it on the current print-wise default
        my $new = $self->default_object_config->clone;
        
        # we override the new config with object-specific options
        my $model_object_config = $object->model_object->config->clone;
        $model_object_config->normalize;
        $new->apply_dynamic($model_object_config);
        
        # check whether the new config is different from the current one
        my $diff = $object->config->diff($new);
        if (@$diff) {
            $object->config->apply($new);
            # TODO: only invalidate changed steps
            $object->_state->invalidate_all;
        }
    }
    
    # handle changes to regions config defaults
    $self->default_region_config->apply_dynamic($config);
    
    # All regions now have distinct settings.
    # Check whether applying the new region config defaults we'd get different regions.
    my $rearrange_regions = 0;
    REGION: foreach my $region_id (0..$#{$self->regions}) {
        foreach my $object (@{$self->objects}) {
            foreach my $volume_id (@{ $object->region_volumes->[$region_id] }) {
                my $volume = $object->model_object->volumes->[$volume_id];
                
                my $new = $self->default_region_config->clone;
                {
                    my $model_object_config = $object->model_object->config->clone;
                    $model_object_config->normalize;
                    $new->apply_dynamic($model_object_config);
                }
                if (defined $volume->material_id) {
                    my $material_config = $object->model_object->model->get_material($volume->material_id)->config->clone;
                    $material_config->normalize;
                    $new->apply_dynamic($material_config);
                }
                if (!$new->equals($self->regions->[$region_id]->config)) {
                    $rearrange_regions = 1;
                    last REGION;
                }
            }
        }
    }
    
    # Some optimization is possible: if the volumes-regions mappings don't change
    # but still region configs are changed somehow, we could just apply the diff
    # and invalidate the affected steps.
    if ($rearrange_regions) {
        # the current subdivision of regions does not make sense anymore.
        # we need to remove all objects and re-add them
        my @model_objects = map $_->model_object, @{$self->objects};
        $self->clear_objects;
        $self->add_model_object($_) for @model_objects;
    }
}

sub has_support_material {
    my $self = shift;
    return (first { $_->config->support_material } @{$self->objects})
        || (first { $_->config->raft_layers > 0 } @{$self->objects})
        || (first { $_->config->support_material_enforce_layers > 0 } @{$self->objects});
}

# caller is responsible for supplying models whose objects don't collide
# and have explicit instance positions
sub add_model_object {
    my $self = shift;
    my ($object, $obj_idx) = @_;
    
    my $object_config = $object->config->clone;
    $object_config->normalize;
    
    my %volumes = ();           # region_id => [ volume_id, ... ]
    foreach my $volume_id (0..$#{$object->volumes}) {
        my $volume = $object->volumes->[$volume_id];
        
        # get the config applied to this volume: start from our global defaults
        my $config = Slic3r::Config::PrintRegion->new;
        $config->apply($self->default_region_config);
        
        # override the defaults with per-object config and then with per-material config
        $config->apply_dynamic($object_config);
        
        if (defined $volume->material_id) {
            my $material_config = $volume->material->config->clone;
            $material_config->normalize;
            $config->apply_dynamic($material_config);
        }
        
        # find an existing print region with the same config
        my $region_id;
        foreach my $i (0..$#{$self->regions}) {
            my $region = $self->regions->[$i];
            if ($config->equals($region->config)) {
                $region_id = $i;
                last;
            }
        }
        
        # if no region exists with the same config, create a new one
        if (!defined $region_id) {
            push @{$self->regions}, my $r = Slic3r::Print::Region->new(
                print  => $self,
            );
            $r->config->apply($config);
            $region_id = $#{$self->regions};
        }
        
        # assign volume to region
        $volumes{$region_id} //= [];
        push @{ $volumes{$region_id} }, $volume_id;
    }
    
    # initialize print object
    my $o = Slic3r::Print::Object->new(
        print               => $self,
        model_object        => $object,
        region_volumes      => [ map $volumes{$_}, 0..$#{$self->regions} ],
        copies              => [ map Slic3r::Point->new_scale(@{ $_->offset }), @{ $object->instances } ],
        layer_height_ranges => $object->layer_height_ranges,
    );
    
    # apply config to print object
    $o->config->apply($self->default_object_config);
    $o->config->apply_dynamic($object_config);
    
    # store print object at the given position
    if (defined $obj_idx) {
        splice @{$self->objects}, $obj_idx, 0, $o;
    } else {
        push @{$self->objects}, $o;
    }
    
    $self->_state->invalidate(STEP_SKIRT);
    $self->_state->invalidate(STEP_BRIM);
}

sub delete_object {
    my ($self, $obj_idx) = @_;
    
    splice @{$self->objects}, $obj_idx, 1;
    # TODO: purge unused regions
    
    $self->_state->invalidate(STEP_SKIRT);
    $self->_state->invalidate(STEP_BRIM);
}

sub clear_objects {
    my ($self) = @_;
    
    @{$self->objects} = ();
    @{$self->regions} = ();
    
    $self->_state->invalidate(STEP_SKIRT);
    $self->_state->invalidate(STEP_BRIM);
}

sub reload_object {
    my ($self, $obj_idx) = @_;
    
    # TODO: this method should check whether the per-object config and per-material configs
    # have changed in such a way that regions need to be rearranged or we can just apply
    # the diff and invalidate something.  Same logic as apply_config()
    # For now we just re-add all objects since we haven't implemented this incremental logic yet.
    # This should also check whether object volumes (parts) have changed.
    
    my @models_objects = map $_->model_object, @{$self->objects};
    $self->clear_objects;
    $self->add_model_object($_) for @models_objects;
}

sub validate {
    my $self = shift;
    
    if ($self->config->complete_objects) {
        # check horizontal clearance
        {
            my @a = ();
            foreach my $object (@{$self->objects}) {
                # get convex hulls of all meshes assigned to this print object
                my @mesh_convex_hulls = map $object->model_object->volumes->[$_]->mesh->convex_hull,
                    map @$_,
                    grep defined $_,
                    @{$object->region_volumes};
                
                # make a single convex hull for all of them
                my $convex_hull = convex_hull([ map @$_, @mesh_convex_hulls ]);
                
                # apply the same transformations we apply to the actual meshes when slicing them
                $object->model_object->instances->[0]->transform_polygon($convex_hull, 1);
        
                # align object to Z = 0 and apply XY shift
                $convex_hull->translate(@{$object->_copies_shift});
                
                # grow convex hull with the clearance margin
                ($convex_hull) = @{offset([$convex_hull], scale $self->config->extruder_clearance_radius / 2, 1, JT_ROUND, scale(0.1))};
                
                # now we need that no instance of $convex_hull does not intersect any of the previously checked object instances
                for my $copy (@{$object->_shifted_copies}) {
                    my $p = $convex_hull->clone;
                    
                    $p->translate(@$copy);
                    if (@{ intersection(\@a, [$p]) }) {
                        die "Some objects are too close; your extruder will collide with them.\n";
                    }
                    @a = @{union([@a, $p])};
                }
            }
        }
        
        # check vertical clearance
        {
            my @object_height = ();
            foreach my $object (@{$self->objects}) {
                my $height = $object->size->z;
                push @object_height, $height for @{$object->copies};
            }
            @object_height = sort { $a <=> $b } @object_height;
            # ignore the tallest *copy* (this is why we repeat height for all of them):
            # it will be printed as last one so its height doesn't matter
            pop @object_height;
            if (@object_height && max(@object_height) > scale $self->config->extruder_clearance_height) {
                die "Some objects are too tall and cannot be printed without extruder collisions.\n";
            }
        }
    }
    
    if ($self->config->spiral_vase) {
        if ((map @{$_->copies}, @{$self->objects}) > 1) {
            die "The Spiral Vase option can only be used when printing a single object.\n";
        }
        if (@{$self->regions} > 1) {
            die "The Spiral Vase option can only be used when printing single material objects.\n";
        }
    }
}

# 0-based indices of used extruders
sub extruders {
    my ($self) = @_;
    
    # initialize all extruder(s) we need
    my @used_extruders = ();
    foreach my $region (@{$self->regions}) {
        push @used_extruders,
            map $region->config->get("${_}_extruder")-1,
            qw(perimeter infill);
    }
    foreach my $object (@{$self->objects}) {
        push @used_extruders,
            map $object->config->get("${_}_extruder")-1,
            qw(support_material support_material_interface);
    }
    
    my %h = map { $_ => 1 } @used_extruders;
    return [ sort keys %h ];
}

sub init_extruders {
    my $self = shift;
    
    # enforce tall skirt if using ooze_prevention
    # FIXME: this is not idempotent (i.e. switching ooze_prevention off will not revert skirt settings)
    if ($self->config->ooze_prevention && @{$self->extruders} > 1) {
        $self->config->set('skirt_height', -1);
        $self->config->set('skirts', 1) if $self->config->skirts == 0;
    }
}

# this value is not supposed to be compared with $layer->id
# since they have different semantics
sub layer_count {
    my $self = shift;
    return max(map $_->layer_count, @{$self->objects});
}

sub regions_count {
    my $self = shift;
    return scalar @{$self->regions};
}

sub bounding_box {
    my $self = shift;
    
    my @points = ();
    foreach my $object (@{$self->objects}) {
        foreach my $copy (@{$object->_shifted_copies}) {
            push @points,
                [ $copy->[X], $copy->[Y] ],
                [ $copy->[X] + $object->size->[X], $copy->[Y] + $object->size->[Y] ];
        }
    }
    return Slic3r::Geometry::BoundingBox->new_from_points([ map Slic3r::Point->new(@$_), @points ]);
}

sub size {
    my $self = shift;
    return $self->bounding_box->size;
}

sub _simplify_slices {
    my $self = shift;
    my ($distance) = @_;
    
    foreach my $layer (map @{$_->layers}, @{$self->objects}) {
        $layer->slices->simplify($distance);
        $_->slices->simplify($distance) for @{$layer->regions};
    }
}

sub process {
    my ($self) = @_;
    
    my $status_cb = $self->status_cb // sub {};
    
    my $print_step = sub {
        my ($step, $cb) = @_;
        if (!$self->_state->done($step)) {
            $self->_state->set_started($step);
            $cb->();
            ### Re-enable this for step-based slicing:
            ### $self->_state->set_done($step);
        }
    };
    my $object_step = sub {
        my ($step, $cb) = @_;
        for my $obj_idx (0..$#{$self->objects}) {
            my $object = $self->objects->[$obj_idx];
            if (!$object->_state->done($step)) {
                $object->_state->set_started($step);
                $cb->($obj_idx);
                ### Re-enable this for step-based slicing:
                ### $object->_state->set_done($step);
            }
        }
    };
    
    # STEP_INIT_EXTRUDERS
    $print_step->(STEP_INIT_EXTRUDERS, sub {
        $self->init_extruders;
    });
    
    # STEP_SLICE
    # skein the STL into layers
    # each layer has surfaces with holes
    $status_cb->(10, "Processing triangulated mesh");
    $object_step->(STEP_SLICE, sub {
        $self->objects->[$_[0]]->slice;
    });
    
    die "No layers were detected. You might want to repair your STL file(s) or check their size and retry.\n"
        if !grep @{$_->layers}, @{$self->objects};
    
    # make perimeters
    # this will add a set of extrusion loops to each layer
    # as well as generate infill boundaries
    $status_cb->(20, "Generating perimeters");
    $object_step->(STEP_PERIMETERS, sub {
        $self->objects->[$_[0]]->make_perimeters;
    });
    
    $status_cb->(30, "Preparing infill");
    $object_step->(STEP_PREPARE_INFILL, sub {
        my $object = $self->objects->[$_[0]];
        
        # this will assign a type (top/bottom/internal) to $layerm->slices
        # and transform $layerm->fill_surfaces from expolygon 
        # to typed top/bottom/internal surfaces;
        $object->detect_surfaces_type;
    
        # decide what surfaces are to be filled
        $_->prepare_fill_surfaces for map @{$_->regions}, @{$object->layers};
    
        # this will detect bridges and reverse bridges
        # and rearrange top/bottom/internal surfaces
        $object->process_external_surfaces;
    
        # detect which fill surfaces are near external layers
        # they will be split in internal and internal-solid surfaces
        $object->discover_horizontal_shells;
        $object->clip_fill_surfaces;
        
        # the following step needs to be done before combination because it may need
        # to remove only half of the combined infill
        $object->bridge_over_infill;
    
        # combine fill surfaces to honor the "infill every N layers" option
        $object->combine_infill;
    });
    
    # this will generate extrusion paths for each layer
    $status_cb->(70, "Infilling layers");
    $object_step->(STEP_INFILL, sub {
        my $object = $self->objects->[$_[0]];
        
        Slic3r::parallelize(
            threads => $self->config->threads,
            items => sub {
                my @items = ();  # [layer_id, region_id]
                for my $region_id (0 .. ($self->regions_count-1)) {
                    push @items, map [$_, $region_id], 0..$#{$object->layers};
                }
                @items;
            },
            thread_cb => sub {
                my $q = shift;
                while (defined (my $obj_layer = $q->dequeue)) {
                    my ($i, $region_id) = @$obj_layer;
                    my $layerm = $object->layers->[$i]->regions->[$region_id];
                    $layerm->fills->append( $object->fill_maker->make_fill($layerm) );
                }
            },
            collect_cb => sub {},
            no_threads_cb => sub {
                foreach my $layerm (map @{$_->regions}, @{$object->layers}) {
                    $layerm->fills->append($object->fill_maker->make_fill($layerm));
                }
            },
        );
    
        ### we could free memory now, but this would make this step not idempotent
        ### $_->fill_surfaces->clear for map @{$_->regions}, @{$object->layers};
    });
    
    # generate support material
    $status_cb->(85, "Generating support material") if $self->has_support_material;
    $object_step->(STEP_SUPPORTMATERIAL, sub {
        $self->objects->[$_[0]]->generate_support_material;
    });
    
    # make skirt
    $status_cb->(88, "Generating skirt/brim");
    $print_step->(STEP_SKIRT, sub {
        $self->make_skirt;
    });
    $print_step->(STEP_BRIM, sub {
        $self->make_brim;  # must come after make_skirt
    });
    
    # time to make some statistics
    if (0) {
        eval "use Devel::Size";
        print  "MEMORY USAGE:\n";
        printf "  meshes        = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->meshes), @{$self->objects})/1024/1024;
        printf "  layer slices  = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  region slices = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  perimeters    = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->perimeters), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  fills         = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->fills), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  print object  = %.1fMb\n", Devel::Size::total_size($self)/1024/1024;
    }
    if (0) {
        eval "use Slic3r::Test::SectionCut";
        Slic3r::Test::SectionCut->new(print => $self)->export_svg("section_cut.svg");
    }
}

sub export_gcode {
    my $self = shift;
    my %params = @_;
    
    my $status_cb = $self->status_cb // sub {};
    
    # output everything to a G-code file
    my $output_file = $self->expanded_output_filepath($params{output_file});
    $status_cb->(90, "Exporting G-code" . ($output_file ? " to $output_file" : ""));
    $self->write_gcode($params{output_fh} || $output_file);
    
    # run post-processing scripts
    if (@{$self->config->post_process}) {
        $status_cb->(95, "Running post-processing scripts");
        $self->config->setenv;
        for (@{$self->config->post_process}) {
            Slic3r::debugf "  '%s' '%s'\n", $_, $output_file;
            system($_, $output_file);
        }
    }
}

sub export_svg {
    my $self = shift;
    my %params = @_;
    
    # is this needed?
    $self->init_extruders;
    
    $_->slice for @{$self->objects};
    
    my $fh = $params{output_fh};
    if (!$fh) {
        my $output_file = $self->expanded_output_filepath($params{output_file});
        $output_file =~ s/\.gcode$/.svg/i;
        Slic3r::open(\$fh, ">", $output_file) or die "Failed to open $output_file for writing\n";
        print "Exporting to $output_file..." unless $params{quiet};
    }
    
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
    
    my @layers = sort { $a->print_z <=> $b->print_z }
        map { @{$_->layers}, @{$_->support_layers} }
        @{$self->objects};
    
    my $layer_id = -1;
    my @previous_layer_slices = ();
    for my $layer (@layers) {
        $layer_id++;
        # TODO: remove slic3r:z for raft layers
        printf $fh qq{  <g id="layer%d" slic3r:z="%s">\n}, $layer_id, unscale($layer->slice_z);
        
        my @current_layer_slices = ();
        # sort slices so that the outermost ones come first
        my @slices = sort { $a->contour->encloses_point($b->contour->[0]) ? 0 : 1 } @{$layer->slices};
        foreach my $copy (@{$layer->object->copies}) {
            foreach my $slice (@slices) {
                my $expolygon = $slice->clone;
                $expolygon->translate(@$copy);
                $print_polygon->($expolygon->contour, 'contour');
                $print_polygon->($_, 'hole') for @{$expolygon->holes};
                push @current_layer_slices, $expolygon;
            }
        }
        # generate support material
        if ($self->has_support_material && $layer->id > 0) {
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
                my $contour = $expolygon->contour;
                my $support_point = $contour->first_point->nearest_point(\@supported_points)
                    or next;
                my $anchor_point = $support_point->nearest_point([ @$contour ]);
                printf $fh qq{    <line x1="%s" y1="%s" x2="%s" y2="%s" style="stroke-width: 2; stroke: white" />\n},
                    map @$_, $support_point, $anchor_point;
            }
        }
        print $fh qq{  </g>\n};
        @previous_layer_slices = @current_layer_slices;
    }
    
    print $fh "</svg>\n";
    close $fh;
    print "Done.\n" unless $params{quiet};
}

sub make_skirt {
    my $self = shift;
    
    # since this method must be idempotent, we clear skirt paths *before*
    # checking whether we need to generate them
    $self->skirt->clear;
    
    return unless $self->config->skirts > 0
        || ($self->config->ooze_prevention && @{$self->extruders} > 1);
    
    # First off we need to decide how tall the skirt must be.
    # The skirt_height option from config is expressed in layers, but our
    # object might have different layer heights, so we need to find the print_z
    # of the highest layer involved.
    # Note that unless skirt_height == -1 (which means it's printed on all layers)
    # the actual skirt might not reach this $skirt_height_z value since the print
    # order of objects on each layer is not guaranteed and will not generally
    # include the thickest object first. It is just guaranteed that a skirt is
    # prepended to the first 'n' layers (with 'n' = skirt_height).
    # $skirt_height_z in this case is the highest possible skirt height for safety.
    my $skirt_height_z = -1;
    foreach my $object (@{$self->objects}) {
        my $skirt_height = ($self->config->skirt_height == -1)
            ? scalar(@{$object->layers})
            : min($self->config->skirt_height, scalar(@{$object->layers}));
        
        my $highest_layer = $object->layers->[$skirt_height-1];
        $skirt_height_z = max($skirt_height_z, $highest_layer->print_z);
    }
    
    # collect points from all layers contained in skirt height
    my @points = ();
    foreach my $object (@{$self->objects}) {
        my @object_points = ();
        
        # get object layers up to $skirt_height_z
        foreach my $layer (@{$object->layers}) {
            last if $layer->print_z > $skirt_height_z;
            push @object_points, map @$_, map @$_, @{$layer->slices};
        }
        
        # get support layers up to $skirt_height_z
        foreach my $layer (@{$object->support_layers}) {
            last if $layer->print_z > $skirt_height_z;
            push @object_points, map @{$_->polyline}, @{$layer->support_fills} if $layer->support_fills;
            push @object_points, map @{$_->polyline}, @{$layer->support_interface_fills} if $layer->support_interface_fills;
        }
        
        # repeat points for each object copy
        foreach my $copy (@{$object->_shifted_copies}) {
            my @copy_points = map $_->clone, @object_points;
            $_->translate(@$copy) for @copy_points;
            push @points, @copy_points;
        }
    }
    return if @points < 3;  # at least three points required for a convex hull
    
    # find out convex hull
    my $convex_hull = convex_hull(\@points);
    
    my @extruded_length = ();  # for each extruder
    
    # skirt may be printed on several layers, having distinct layer heights,
    # but loops must be aligned so can't vary width/spacing
    # TODO: use each extruder's own flow
    my $first_layer_height = $self->objects->[0]->config->get_value('first_layer_height');
    my $flow = Slic3r::Flow->new_from_width(
        width               => ($self->config->first_layer_extrusion_width || $self->regions->[0]->config->perimeter_extrusion_width),
        role                => FLOW_ROLE_PERIMETER,
        nozzle_diameter     => $self->config->nozzle_diameter->[0],
        layer_height        => $first_layer_height,
        bridge_flow_ratio   => 0,
    );
    my $spacing = $flow->spacing;
    my $mm3_per_mm = $flow->mm3_per_mm($first_layer_height);
    
    my @extruders_e_per_mm = ();
    my $extruder_idx = 0;
    
    # draw outlines from outside to inside
    # loop while we have less skirts than required or any extruder hasn't reached the min length if any
    my $distance = scale $self->config->skirt_distance;
    for (my $i = $self->config->skirts; $i > 0; $i--) {
        $distance += scale $spacing;
        my $loop = offset([$convex_hull], $distance, 1, JT_ROUND, scale(0.1))->[0];
        $self->skirt->append(Slic3r::ExtrusionLoop->new_from_paths(
            Slic3r::ExtrusionPath->new(
                polyline        => Slic3r::Polygon->new(@$loop)->split_at_first_point,
                role            => EXTR_ROLE_SKIRT,
                mm3_per_mm      => $mm3_per_mm,
                width           => $flow->width,
                height          => $first_layer_height,
            ),
        ));
        
        if ($self->config->min_skirt_length > 0) {
            $extruded_length[$extruder_idx] ||= 0;
            if (!$extruders_e_per_mm[$extruder_idx]) {
                my $extruder = Slic3r::Extruder->new($extruder_idx, $self->config);
                $extruders_e_per_mm[$extruder_idx] = $extruder->e_per_mm($mm3_per_mm);
            }
            $extruded_length[$extruder_idx] += unscale $loop->length * $extruders_e_per_mm[$extruder_idx];
            $i++ if defined first { ($extruded_length[$_] // 0) < $self->config->min_skirt_length } 0 .. $#{$self->extruders};
            if ($extruded_length[$extruder_idx] >= $self->config->min_skirt_length) {
                if ($extruder_idx < $#{$self->extruders}) {
                    $extruder_idx++;
                    next;
                }
            }
        }
    }
    
    $self->skirt->reverse;
}

sub make_brim {
    my $self = shift;
    
    # since this method must be idempotent, we clear brim paths *before*
    # checking whether we need to generate them
    $self->brim->clear;
    
    return unless $self->config->brim_width > 0;
    
    # brim is only printed on first layer and uses support material extruder
    my $first_layer_height = $self->objects->[0]->config->get_abs_value('first_layer_height');
    my $flow = Slic3r::Flow->new_from_width(
        width               => ($self->config->first_layer_extrusion_width || $self->regions->[0]->config->perimeter_extrusion_width),
        role                => FLOW_ROLE_PERIMETER,
        nozzle_diameter     => $self->config->get_at('nozzle_diameter', $self->objects->[0]->config->support_material_extruder-1),
        layer_height        => $first_layer_height,
        bridge_flow_ratio   => 0,
    );
    my $mm3_per_mm = $flow->mm3_per_mm($first_layer_height);
    
    my $grow_distance = $flow->scaled_width / 2;
    my @islands = (); # array of polygons
    foreach my $obj_idx (0 .. $#{$self->objects}) {
        my $object = $self->objects->[$obj_idx];
        my $layer0 = $object->layers->[0];
        my @object_islands = (
            (map $_->contour, @{$layer0->slices}),
        );
        if (@{ $object->support_layers }) {
            my $support_layer0 = $object->support_layers->[0];
            push @object_islands,
                (map @{$_->polyline->grow($grow_distance)}, @{$support_layer0->support_fills})
                if $support_layer0->support_fills;
            push @object_islands,
                (map @{$_->polyline->grow($grow_distance)}, @{$support_layer0->support_interface_fills})
                if $support_layer0->support_interface_fills;
        }
        foreach my $copy (@{$object->_shifted_copies}) {
            push @islands, map { $_->translate(@$copy); $_ } map $_->clone, @object_islands;
        }
    }
    
    # if brim touches skirt, make it around skirt too
    # TODO: calculate actual skirt width (using each extruder's flow in multi-extruder setups)
    if ($self->config->skirt_distance + (($self->config->skirts - 1) * $flow->spacing) <= $self->config->brim_width) {
        push @islands, map @{$_->split_at_first_point->polyline->grow($grow_distance)}, @{$self->skirt};
    }
    
    my @loops = ();
    my $num_loops = sprintf "%.0f", $self->config->brim_width / $flow->width;
    for my $i (reverse 1 .. $num_loops) {
        # JT_SQUARE ensures no vertex is outside the given offset distance
        # -0.5 because islands are not represented by their centerlines
        # (first offset more, then step back - reverse order than the one used for 
        # perimeters because here we're offsetting outwards)
        push @loops, @{offset2(\@islands, ($i + 0.5) * $flow->scaled_spacing, -1.0 * $flow->scaled_spacing, 100000, JT_SQUARE)};
    }
    
    $self->brim->append(map Slic3r::ExtrusionLoop->new_from_paths(
        Slic3r::ExtrusionPath->new(
            polyline        => Slic3r::Polygon->new(@$_)->split_at_first_point,
            role            => EXTR_ROLE_SKIRT,
            mm3_per_mm      => $mm3_per_mm,
            width           => $flow->width,
            height          => $first_layer_height,
        ),
    ), reverse @{union_pt_chained(\@loops)});
}

sub write_gcode {
    my $self = shift;
    my ($file) = @_;
    
    # open output gcode file if we weren't supplied a file-handle
    my $fh;
    if (ref $file eq 'IO::Scalar') {
        $fh = $file;
    } else {
        Slic3r::open(\$fh, ">", $file)
            or die "Failed to open $file for writing\n";
        
        # enable UTF-8 output since user might have entered Unicode characters in fields like notes
        binmode $fh, ':utf8';
    }
    
    
    # write some information
    my @lt = localtime;
    printf $fh "; generated by Slic3r $Slic3r::VERSION on %04d-%02d-%02d at %02d:%02d:%02d\n\n",
        $lt[5] + 1900, $lt[4]+1, $lt[3], $lt[2], $lt[1], $lt[0];

    print $fh "; $_\n" foreach split /\R/, $self->config->notes;
    print $fh "\n" if $self->config->notes;
    
    my $first_object = $self->objects->[0];
    my $layer_height = $first_object->config->layer_height;
    for my $region_id (0..$#{$self->regions}) {
        printf $fh "; perimeters extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_PERIMETER, $layer_height, 0, 0, undef, $first_object)->width;
        printf $fh "; infill extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_INFILL, $layer_height, 0, 0, undef, $first_object)->width;
        printf $fh "; solid infill extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_SOLID_INFILL, $layer_height, 0, 0, undef, $first_object)->width;
        printf $fh "; top infill extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_TOP_SOLID_INFILL, $layer_height, 0, 0, undef, $first_object)->width;
        printf $fh "; support material extrusion width = %.2fmm\n",
            $self->objects->[0]->support_material_flow->width
            if $self->has_support_material;
        printf $fh "; first layer extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_PERIMETER, $layer_height, 0, 1, undef, $self->objects->[0])->width
            if $self->regions->[$region_id]->config->first_layer_extrusion_width;
        print  $fh "\n";
    }
    
    # prepare the helper object for replacing placeholders in custom G-code and output filename
    $self->placeholder_parser->update_timestamp;
    
    # set up our helper object
    my $gcodegen = Slic3r::GCode->new(
        placeholder_parser  => $self->placeholder_parser,
        layer_count         => $self->layer_count,
    );
    $gcodegen->config->apply_print_config($self->config);
    $gcodegen->set_extruders($self->extruders, $self->config);
    
    print $fh "G21 ; set units to millimeters\n" if $self->config->gcode_flavor ne 'makerware';
    print $fh $gcodegen->set_fan(0, 1) if $self->config->cooling && $self->config->disable_fan_first_layers;
    
    # set bed temperature
    if ((my $temp = $self->config->first_layer_bed_temperature) && $self->config->start_gcode !~ /M(?:190|140)/i) {
        printf $fh $gcodegen->set_bed_temperature($temp, 1);
    }
    
    # set extruder(s) temperature before and after start G-code
    my $print_first_layer_temperature = sub {
        my ($wait) = @_;
        
        return if $self->config->start_gcode =~ /M(?:109|104)/i;
        for my $t (@{$self->extruders}) {
            my $temp = $self->config->get_at('first_layer_temperature', $t);
            $temp += $self->config->standby_temperature_delta if $self->config->ooze_prevention;
            printf $fh $gcodegen->set_temperature($temp, $wait, $t) if $temp > 0;
        }
    };
    $print_first_layer_temperature->(0);
    printf $fh "%s\n", $gcodegen->placeholder_parser->process($self->config->start_gcode);
    $print_first_layer_temperature->(1);
    
    # set other general things
    print  $fh "G90 ; use absolute coordinates\n" if $self->config->gcode_flavor ne 'makerware';
    if ($self->config->gcode_flavor =~ /^(?:reprap|teacup)$/) {
        printf $fh $gcodegen->reset_e;
        if ($self->config->use_relative_e_distances) {
            print $fh "M83 ; use relative distances for extrusion\n";
        } else {
            print $fh "M82 ; use absolute distances for extrusion\n";
        }
    }
    
    # initialize a motion planner for object-to-object travel moves
    if ($self->config->avoid_crossing_perimeters) {
        my $distance_from_objects = 1;
        # compute the offsetted convex hull for each object and repeat it for each copy.
        my @islands = ();
        foreach my $obj_idx (0 .. $#{$self->objects}) {
            my $convex_hull = convex_hull([
                map @{$_->contour}, map @{$_->slices}, @{$self->objects->[$obj_idx]->layers},
            ]);
            # discard layers only containing thin walls (offset would fail on an empty polygon)
            if (@$convex_hull) {
                my $expolygon = Slic3r::ExPolygon->new($convex_hull);
                my @island = @{$expolygon->offset_ex(scale $distance_from_objects, 1, JT_SQUARE)};
                foreach my $copy (@{ $self->objects->[$obj_idx]->_shifted_copies }) {
                    push @islands, map { my $c = $_->clone; $c->translate(@$copy); $c } @island;
                }
            }
        }
        $gcodegen->external_mp(Slic3r::GCode::MotionPlanner->new(
            islands     => union_ex([ map @$_, @islands ]),
            internal    => 0,
        ));
    }
    
    # calculate wiping points if needed
    if ($self->config->ooze_prevention) {
        my @skirt_points = map @$_, map @$_, @{$self->skirt};
        if (@skirt_points) {
            my $outer_skirt = convex_hull(\@skirt_points);
            my @skirts = ();
            foreach my $extruder_id (@{$self->extruders}) {
                push @skirts, my $s = $outer_skirt->clone;
                $s->translate(map scale($_), @{$self->config->get_at('extruder_offset', $extruder_id)});
            }
            my $convex_hull = convex_hull([ map @$_, @skirts ]);
            $gcodegen->standby_points([ map $_->clone, map @$_, map $_->subdivide(scale 10), @{offset([$convex_hull], scale 3)} ]);
        }
    }
    
    # prepare the layer processor
    my $layer_gcode = Slic3r::GCode::Layer->new(
        print       => $self,
        gcodegen    => $gcodegen,
    );
    
    # set initial extruder only after custom start G-code
    print $fh $gcodegen->set_extruder($self->extruders->[0]);
    
    # do all objects for each layer
    if ($self->config->complete_objects) {
        # print objects from the smallest to the tallest to avoid collisions
        # when moving onto next object starting point
        my @obj_idx = sort { $self->objects->[$a]->size->[Z] <=> $self->objects->[$b]->size->[Z] } 0..$#{$self->objects};
        
        my $finished_objects = 0;
        for my $obj_idx (@obj_idx) {
            for my $copy (@{ $self->objects->[$obj_idx]->_shifted_copies }) {
                # move to the origin position for the copy we're going to print.
                # this happens before Z goes down to layer 0 again, so that 
                # no collision happens hopefully.
                if ($finished_objects > 0) {
                    $gcodegen->set_shift(map unscale $copy->[$_], X,Y);
                    print $fh $gcodegen->retract;
                    print $fh $gcodegen->G0(Slic3r::Point->new(0,0), undef, 0, $gcodegen->config->travel_speed*60, 'move to origin position for next object');
                }
                
                my $buffer = Slic3r::GCode::CoolingBuffer->new(
                    config      => $self->config,
                    gcodegen    => $gcodegen,
                );
                
                my $object = $self->objects->[$obj_idx];
                my @layers = sort { $a->print_z <=> $b->print_z } @{$object->layers}, @{$object->support_layers};
                for my $layer (@layers) {
                    # if we are printing the bottom layer of an object, and we have already finished
                    # another one, set first layer temperatures. this happens before the Z move
                    # is triggered, so machine has more time to reach such temperatures
                    if ($layer->id == 0 && $finished_objects > 0) {
                        printf $fh $gcodegen->set_bed_temperature($self->config->first_layer_bed_temperature),
                            if $self->config->first_layer_bed_temperature;
                        $print_first_layer_temperature->();
                    }
                    print $fh $buffer->append(
                        $layer_gcode->process_layer($layer, [$copy]),
                        $layer->object."",
                        $layer->id,
                        $layer->print_z,
                    );
                }
                print $fh $buffer->flush;
                $finished_objects++;
            }
        }
    } else {
        # order objects using a nearest neighbor search
        my @obj_idx = @{chained_path([ map Slic3r::Point->new(@{$_->_shifted_copies->[0]}), @{$self->objects} ])};
        
        # sort layers by Z
        my %layers = ();  # print_z => [ [layers], [layers], [layers] ]  by obj_idx
        foreach my $obj_idx (0 .. $#{$self->objects}) {
            my $object = $self->objects->[$obj_idx];
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                $layers{ $layer->print_z } ||= [];
                $layers{ $layer->print_z }[$obj_idx] ||= [];
                push @{$layers{ $layer->print_z }[$obj_idx]}, $layer;
            }
        }
        
        my $buffer = Slic3r::GCode::CoolingBuffer->new(
            config      => $self->config,
            gcodegen    => $gcodegen,
        );
        foreach my $print_z (sort { $a <=> $b } keys %layers) {
            foreach my $obj_idx (@obj_idx) {
                foreach my $layer (@{ $layers{$print_z}[$obj_idx] // [] }) {
                    print $fh $buffer->append(
                        $layer_gcode->process_layer($layer, $layer->object->_shifted_copies),
                        $layer->object . ref($layer),  # differentiate $obj_id between normal layers and support layers
                        $layer->id,
                        $layer->print_z,
                    );
                }
            }
        }
        print $fh $buffer->flush;
    }
    
    # write end commands to file
    print $fh $gcodegen->retract if $gcodegen->extruder;  # empty prints don't even set an extruder
    print $fh $gcodegen->set_fan(0);
    printf $fh "%s\n", $gcodegen->placeholder_parser->process($self->config->end_gcode);
    
    $self->total_used_filament(0);
    $self->total_extruded_volume(0);
    foreach my $extruder_id (@{$self->extruders}) {
        my $extruder = $gcodegen->extruders->{$extruder_id};
        # the final retraction doesn't really count as "used filament"
        my $used_filament = $extruder->absolute_E + $extruder->retract_length;
        my $extruded_volume = $extruder->extruded_volume($used_filament);
        
        printf $fh "; filament used = %.1fmm (%.1fcm3)\n",
            $used_filament, $extruded_volume/1000;
        
        $self->total_used_filament($self->total_used_filament + $used_filament);
        $self->total_extruded_volume($self->total_extruded_volume + $extruded_volume);
    }
    
    # append full config
    print $fh "\n";
    foreach my $config ($self->config, $self->default_object_config, $self->default_region_config) {
        foreach my $opt_key (sort @{$config->get_keys}) {
            next if $Slic3r::Config::Options->{$opt_key}{shortcut};
            printf $fh "; %s = %s\n", $opt_key, $config->serialize($opt_key);
        }
    }
    
    # close our gcode file
    close $fh;
}

# this method will return the supplied input file path after expanding its
# format variables with their values
sub expanded_output_filepath {
    my $self = shift;
    my ($path) = @_;
    
    return undef if !@{$self->objects};
    my $input_file = first { defined $_ } map $_->model_object->input_file, @{$self->objects};
    return undef if !defined $input_file;
    
    my $filename = my $filename_base = basename($input_file);
    $filename_base =~ s/\.[^.]+$//;  # without suffix
    my $extra = {
        input_filename      => $filename,
        input_filename_base => $filename_base,
    };
    
    if ($path && -d $path) {
        # if output path is an existing directory, we take that and append
        # the specified filename format
        $path = File::Spec->join($path, $self->config->output_filename_format);
    } elsif (!$path) {
        # if no explicit output file was defined, we take the input
        # file directory and append the specified filename format
        $path = (fileparse($input_file))[1] . $self->config->output_filename_format;
    } else {
        # path is a full path to a file so we use it as it is
    }
    
    # make sure we use an up-to-date timestamp
    $self->placeholder_parser->update_timestamp;
    return $self->placeholder_parser->process($path, $extra);
}

sub invalidate_step {
    my ($self, $step, $obj_idx) = @_;
    
    # invalidate $step in the correct state object
    if ($Slic3r::Print::State::print_step->{$step}) {
        $self->_state->invalidate($step);
    } else {
        # object step
        if (defined $obj_idx) {
            $self->objects->[$obj_idx]->_state->invalidate($step);
        } else {
            $_->_state->invalidate($step) for @{$self->objects};
        }
    }
    
    # recursively invalidate steps depending on $step
    $self->invalidate_step($_)
        for grep { grep { $_ == $step } @{$Slic3r::Print::State::prereqs{$_}} }
            keys %Slic3r::Print::State::prereqs;
}

# This method assigns extruders to the volumes having a material
# but not having extruders set in the material config.
sub auto_assign_extruders {
    my ($self, $model_object) = @_;
    
    # only assign extruders if object has more than one volume
    return if @{$model_object->volumes} == 1;
    
    my $extruders = scalar @{ $self->config->nozzle_diameter };
    foreach my $i (0..$#{$model_object->volumes}) {
        my $volume = $model_object->volumes->[$i];
        if (defined $volume->material_id) {
            my $material = $model_object->model->get_material($volume->material_id);
            my $config = $material->config;
            my $extruder_id = $i + 1;
            $config->set_ifndef('extruder', $extruder_id);
        }
    }
}

1;
