package Slic3r::Model;

use List::Util qw(first max);
use Slic3r::Geometry qw(X Y Z move_points);

sub read_from_file {
    my $class = shift;
    my ($input_file) = @_;
    
    my $model = $input_file =~ /\.stl$/i            ? Slic3r::Format::STL->read_file($input_file)
              : $input_file =~ /\.obj$/i            ? Slic3r::Format::OBJ->read_file($input_file)
              : $input_file =~ /\.amf(\.xml)?$/i    ? Slic3r::Format::AMF->read_file($input_file)
              : die "Input file must have .stl, .obj or .amf(.xml) extension\n";
    
    $_->set_input_file($input_file) for @{$model->objects};
    return $model;
}

sub merge {
    my $class = shift;
    my @models = @_;
    
    my $new_model = ref($class)
        ? $class
        : $class->new;
    
    $new_model->add_object($_) for map @{$_->objects}, @models;
    return $new_model;
}

sub add_object {
    my $self = shift;
    
    if (@_ == 1) {
        # we have a Model::Object
        my ($object) = @_;
        return $self->_add_object_clone($object);
    } else {
        my (%args) = @_;
        
        my $new_object = $self->_add_object;
        
        $new_object->set_input_file($args{input_file})
            if defined $args{input_file};
        $new_object->config->apply($args{config})
            if defined $args{config};
        $new_object->set_layer_height_ranges($args{layer_height_ranges})
            if defined $args{layer_height_ranges};
        $new_object->set_origin_translation($args{origin_translation})
            if defined $args{origin_translation};
        
        return $new_object;
    }
}

sub set_material {
    my $self = shift;
    my ($material_id, $attributes) = @_;
    
    my $material = $self->add_material($material_id);
    $material->apply($attributes // {});
    return $material;
}

sub duplicate_objects_grid {
    my ($self, $grid, $distance) = @_;

    die "Grid duplication is not supported with multiple objects\n"
        if @{$self->objects} > 1;

    my $object = $self->objects->[0];
    $object->clear_instances;

    my $size = $object->bounding_box->size;
    for my $x_copy (1..$grid->[X]) {
        for my $y_copy (1..$grid->[Y]) {
            $object->add_instance(
                offset => Slic3r::Pointf->new(
                    ($size->[X] + $distance) * ($x_copy-1),
                    ($size->[Y] + $distance) * ($y_copy-1),
                ),
            );
        }
    }
}

# this will append more instances to each object
# and then automatically rearrange everything
sub duplicate_objects {
    my ($self, $copies_num, $distance, $bb) = @_;
    
    foreach my $object (@{$self->objects}) {
        my @instances = @{$object->instances};
        foreach my $instance (@instances) {
            $object->add_instance($instance) for 2..$copies_num;
        }
    }
    
    $self->arrange_objects($distance, $bb);
}

# arrange objects preserving their instance count
# but altering their instance positions
sub arrange_objects {
    my ($self, $distance, $bb) = @_;
    
    # get the (transformed) size of each instance so that we take
    # into account their different transformations when packing
    my @instance_sizes = ();
    foreach my $object (@{$self->objects}) {
        push @instance_sizes, map $object->instance_bounding_box($_)->size, 0..$#{$object->instances};
    }
    
    my @positions = $self->_arrange(\@instance_sizes, $distance, $bb);
    
    foreach my $object (@{$self->objects}) {
        $_->set_offset(Slic3r::Pointf->new(@{shift @positions})) for @{$object->instances};
        $object->update_bounding_box;
    }
}

# duplicate the entire model preserving instance relative positions
sub duplicate {
    my ($self, $copies_num, $distance, $bb) = @_;
    
    my $model_size = $self->bounding_box->size;
    my @positions = $self->_arrange([ map $model_size, 2..$copies_num ], $distance, $bb);
    
    # note that this will leave the object count unaltered
    
    foreach my $object (@{$self->objects}) {
        my @instances = @{$object->instances};  # store separately to avoid recursion from add_instance() below
        foreach my $instance (@instances) {
            foreach my $pos (@positions) {
                $object->add_instance(
                    offset          => Slic3r::Pointf->new($instance->offset->[X] + $pos->[X], $instance->offset->[Y] + $pos->[Y]),
                    rotation        => $instance->rotation,
                    scaling_factor  => $instance->scaling_factor,
                );
            }
        }
        $object->update_bounding_box;
    }
}

sub _arrange {
    my ($self, $sizes, $distance, $bb) = @_;
    
    # we supply unscaled data to arrange()
    return Slic3r::Geometry::arrange(
        scalar(@$sizes),                # number of parts
        max(map $_->x, @$sizes),        # cell width
        max(map $_->y, @$sizes),        # cell height ,
        $distance,                      # distance between cells
        $bb,                            # bounding box of the area to fill (can be undef)
    );
}

sub has_objects_with_no_instances {
    my ($self) = @_;
    return (first { !defined $_->instances } @{$self->objects}) ? 1 : 0;
}

# makes sure all objects have at least one instance
sub add_default_instances {
    my ($self) = @_;
    
    # apply a default position to all objects not having one
    my $added = 0;
    foreach my $object (@{$self->objects}) {
        if ($object->instances_count == 0) {
            $object->add_instance(offset => Slic3r::Pointf->new(0,0));
            $added = 1;
        }
    }
    return $added;
}

# this returns the bounding box of the *transformed* instances
sub bounding_box {
    my $self = shift;
    
    return undef if !@{$self->objects};
    my $bb = $self->objects->[0]->bounding_box;
    $bb->merge($_->bounding_box) for @{$self->objects}[1..$#{$self->objects}];
    return $bb;
}

# input point is expressed in unscaled coordinates
sub center_instances_around_point {
    my ($self, $point) = @_;
    
    my $bb = $self->bounding_box;
    return if !defined $bb;
    
    my $size = $bb->size;
    my @shift = (
        -$bb->x_min + $point->[X] - $size->x/2,
        -$bb->y_min + $point->[Y] - $size->y/2,  #//
    );
    
    foreach my $object (@{$self->objects}) {
        foreach my $instance (@{$object->instances}) {
            $instance->set_offset(Slic3r::Pointf->new(
                $instance->offset->x + $shift[X],
                $instance->offset->y + $shift[Y],  #++
            ));
        }
        $object->update_bounding_box;
    }
}

sub align_instances_to_origin {
    my ($self) = @_;
    
    my $bb = $self->bounding_box;
    return if !defined $bb;
    
    my $new_center = $bb->size;
    $new_center->translate(-$new_center->x/2, -$new_center->y/2);  #//
    $self->center_instances_around_point($new_center);
}

sub translate {
    my $self = shift;
    my @shift = @_;
    
    $_->translate(@shift) for @{$self->objects};
}

# flattens everything to a single mesh
sub mesh {
    my $self = shift;
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->merge($_->mesh) for @{$self->objects};
    return $mesh;
}

# flattens everything to a single mesh
sub raw_mesh {
    my $self = shift;
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->merge($_->raw_mesh) for @{$self->objects};
    return $mesh;
}

# this method splits objects into multiple distinct objects by walking their meshes
sub split_meshes {
    my $self = shift;
    
    my @objects = @{$self->objects};
    @{$self->objects} = ();
    
    foreach my $object (@objects) {
        if (@{$object->volumes} > 1) {
            # We can't split meshes if there's more than one material, because
            # we can't group the resulting meshes by object afterwards
            $self->_add_object($object);
            next;
        }
        
        my $volume = $object->volumes->[0];
        foreach my $mesh (@{$volume->mesh->split}) {
            my $new_object = $self->add_object(
                input_file          => $object->input_file,
                config              => $object->config->clone,
                layer_height_ranges => $object->layer_height_ranges,   # TODO: this needs to be cloned
                origin_translation  => $object->origin_translation,
            );
            $new_object->add_volume(
                mesh        => $mesh,
                material_id => $volume->material_id,
            );
            
            # add one instance per original instance
            $new_object->add_instance(
                offset          => Slic3r::Pointf->new(@{$_->offset}),
                rotation        => $_->rotation,
                scaling_factor  => $_->scaling_factor,
            ) for @{ $object->instances // [] };
        }
    }
}

sub print_info {
    my $self = shift;
    $_->print_info for @{$self->objects};
}

sub get_material_name {
    my $self = shift;
    my ($material_id) = @_;
    
    my $name;
    if ($self->has_material($material_id)) {
        $name //= $self->get_material($material_id)
            ->attributes->{$_} for qw(Name name);
    }
    $name //= $material_id;
    return $name;
}

package Slic3r::Model::Material;

sub apply {
    my ($self, $attributes) = @_;
    $self->set_attribute($_, $attributes{$_}) for keys %$attributes;
}

package Slic3r::Model::Object;

use File::Basename qw(basename);
use List::Util qw(first sum);
use Slic3r::Geometry qw(X Y Z rad2deg);

sub add_volume {
    my $self = shift;

    my $new_volume;
    if (@_ == 1) {
        # we have a Model::Volume
        my ($volume) = @_;
        
        $new_volume = $self->_add_volume_clone($volume);
        
        # TODO: material_id can't be undef.
        if (defined $volume->material_id) {
            #  merge material attributes and config (should we rename materials in case of duplicates?)
            if (my $material = $volume->object->model->get_material($volume->material_id)) {
                my %attributes = %{ $material->attributes };
                if ($self->model->has_material($volume->material_id)) {
                    %attributes = (%attributes, %{ $self->model->get_material($volume->material_id)->attributes })
                }
                my $new_material = $self->model->set_material($volume->material_id, {%attributes});
                $new_material->config->apply($material->config);
            }
        }
    } else {
        my %args = @_;
        
        $new_volume = $self->_add_volume($args{mesh});
        
        $new_volume->set_material_id($args{material_id})
            if defined $args{material_id};
        $new_volume->set_modifier($args{modifier})
            if defined $args{modifier};
    }
    
    if (defined $new_volume->material_id && !defined $self->model->get_material($new_volume->material_id)) {
        # TODO: this should be a trigger on Volume::material_id
        $self->model->set_material($new_volume->material_id);
    }
    
    $self->invalidate_bounding_box;
    
    return $new_volume;
}

sub add_instance {
    my $self = shift;
    my %params = @_;
    
    if (@_ == 1) {
        # we have a Model::Instance
        my ($instance) = @_;
        return $self->_add_instance_clone($instance);
    } else {
        my (%args) = @_;
        
        my $new_instance = $self->_add_instance;
        
        $new_instance->set_rotation($args{rotation})
            if defined $args{rotation};
        $new_instance->set_scaling_factor($args{scaling_factor})
            if defined $args{scaling_factor};
        $new_instance->set_offset($args{offset})
            if defined $args{offset};
        
        return $new_instance;
    }
}

sub raw_mesh {
    my $self = shift;
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->merge($_->mesh) for grep !$_->modifier, @{ $self->volumes };
    return $mesh;
}

sub raw_bounding_box {
    my $self = shift;
    
    my @meshes = map $_->mesh->clone, grep !$_->modifier, @{ $self->volumes };
    die "No meshes found" if !@meshes;
    
    my $instance = $self->instances->[0];
    $instance->transform_mesh($_, 1) for @meshes;
    
    my $bb = (shift @meshes)->bounding_box;
    $bb->merge($_->bounding_box) for @meshes;
    return $bb;
}

# flattens all volumes and instances into a single mesh
sub mesh {
    my $self = shift;
    
    my $mesh = $self->raw_mesh;
    
    my @instance_meshes = ();
    foreach my $instance (@{ $self->instances }) {
        my $m = $mesh->clone;
        $instance->transform_mesh($m);
        push @instance_meshes, $m;
    }
    
    my $full_mesh = Slic3r::TriangleMesh->new;
    $full_mesh->merge($_) for @instance_meshes;
    return $full_mesh;
}

sub update_bounding_box {
    my ($self) = @_;
    $self->_bounding_box($self->mesh->bounding_box);
}

# this returns the bounding box of the *transformed* instances
sub bounding_box {
    my $self = shift;
    
    $self->update_bounding_box if !defined $self->_bounding_box;
    return $self->_bounding_box->clone;
}

# this returns the bounding box of the *transformed* given instance
sub instance_bounding_box {
    my ($self, $instance_idx) = @_;
    
    my $mesh = $self->raw_mesh;
    $self->instances->[$instance_idx]->transform_mesh($mesh);
    return $mesh->bounding_box;
}

sub center_around_origin {
    my $self = shift;
    
    # calculate the displacements needed to 
    # center this object around the origin
    my $bb = $self->raw_mesh->bounding_box;
    
    # first align to origin on XY
    my @shift = (
        -$bb->x_min,
        -$bb->y_min,
        0,
    );
    
    # then center it on XY
    my $size = $bb->size;
    $shift[X] -= $size->x/2;
    $shift[Y] -= $size->y/2;  #//
    
    $self->translate(@shift);
    $self->origin_translation->translate(@shift[X,Y]);
    
    if ($self->instances_count > 0) {
        foreach my $instance (@{ $self->instances }) {
            $instance->set_offset(Slic3r::Pointf->new(
                $instance->offset->x - $shift[X],
                $instance->offset->y - $shift[Y],   #--
            ));
        }
        $self->update_bounding_box;
    }
    
    return @shift;
}

sub translate {
    my $self = shift;
    my @shift = @_;
    
    $_->mesh->translate(@shift) for @{$self->volumes};
    $self->_bounding_box->translate(@shift) if defined $self->_bounding_box;
}

sub rotate {
    my ($self, $angle, $axis) = @_;
    
    # we accept angle in radians but mesh currently uses degrees
    $angle = rad2deg($angle);
    
    if ($axis == X) {
        $_->mesh->rotate_x($angle) for @{$self->volumes};
    } elsif ($axis == Y) {
        $_->mesh->rotate_y($angle) for @{$self->volumes};
    } elsif ($axis == Z) {
        $_->mesh->rotate_z($angle) for @{$self->volumes};
    }
    $self->invalidate_bounding_box;
}

sub flip {
    my ($self, $axis) = @_;
    
    if ($axis == X) {
        $_->mesh->flip_x for @{$self->volumes};
    } elsif ($axis == Y) {
        $_->mesh->flip_y for @{$self->volumes};
    } elsif ($axis == Z) {
        $_->mesh->flip_z for @{$self->volumes};
    }
    $self->invalidate_bounding_box;
}

sub scale_xyz {
    my ($self, $versor) = @_;
    
    $_->mesh->scale_xyz($versor) for @{$self->volumes};
    $self->invalidate_bounding_box;
}

sub materials_count {
    my $self = shift;
    
    my %materials = map { $_->material_id // '_default' => 1 } @{$self->volumes};
    return scalar keys %materials;
}

sub unique_materials {
    my $self = shift;
    
    my %materials = ();
    $materials{ $_->material_id } = 1
        for grep { defined $_->material_id } @{$self->volumes};
    return sort keys %materials;
}

sub facets_count {
    my $self = shift;
    return sum(map $_->mesh->facets_count, grep !$_->modifier, @{$self->volumes});
}

sub needed_repair {
    my $self = shift;
    return (first { !$_->mesh->needed_repair } grep !$_->modifier, @{$self->volumes}) ? 0 : 1;
}

sub mesh_stats {
    my $self = shift;
    
    # TODO: sum values from all volumes
    return $self->volumes->[0]->mesh->stats;
}

sub print_info {
    my $self = shift;
    
    printf "Info about %s:\n", basename($self->input_file);
    printf "  size:              x=%.3f y=%.3f z=%.3f\n", @{$self->raw_mesh->bounding_box->size};
    if (my $stats = $self->mesh_stats) {
        printf "  number of facets:  %d\n", $stats->{number_of_facets};
        printf "  number of shells:  %d\n", $stats->{number_of_parts};
        printf "  volume:            %.3f\n", $stats->{volume};
        if ($self->needed_repair) {
            printf "  needed repair:     yes\n";
            printf "  degenerate facets: %d\n", $stats->{degenerate_facets};
            printf "  edges fixed:       %d\n", $stats->{edges_fixed};
            printf "  facets removed:    %d\n", $stats->{facets_removed};
            printf "  facets added:      %d\n", $stats->{facets_added};
            printf "  facets reversed:   %d\n", $stats->{facets_reversed};
            printf "  backwards edges:   %d\n", $stats->{backwards_edges};
        } else {
            printf "  needed repair:     no\n";
        }
    } else {
        printf "  number of facets:  %d\n", scalar(map @{$_->facets}, grep !$_->modifier, @{$self->volumes});
    }
}

sub cut {
    my ($self, $z) = @_;
    
    # clone this one to duplicate instances, materials etc.
    my $model = Slic3r::Model->new;
    my $upper = $model->add_object($self);
    my $lower = $model->add_object($self);
    $upper->clear_volumes;
    $lower->clear_volumes;
    
    foreach my $volume (@{$self->volumes}) {
        if ($volume->modifier) {
            # don't cut modifiers
            $upper->add_volume($volume);
            $lower->add_volume($volume);
        } else {
            my $upper_mesh = Slic3r::TriangleMesh->new;
            my $lower_mesh = Slic3r::TriangleMesh->new;
            $volume->mesh->cut($z + $volume->mesh->bounding_box->z_min, $upper_mesh, $lower_mesh);
            $upper_mesh->repair;
            $lower_mesh->repair;
            $upper_mesh->reset_repair_stats;
            $lower_mesh->reset_repair_stats;
            
            if ($upper_mesh->facets_count > 0) {
                $upper->add_volume(
                    material_id => $volume->material_id,
                    mesh        => $upper_mesh,
                    modifier    => $volume->modifier,
                );
            }
            if ($lower_mesh->facets_count > 0) {
                $lower->add_volume(
                    material_id => $volume->material_id,
                    mesh        => $lower_mesh,
                    modifier    => $volume->modifier,
                );
            }
        }
    }
    
    $upper = undef if !@{$upper->volumes};
    $lower = undef if !@{$lower->volumes};
    return ($model, $upper, $lower);
}

package Slic3r::Model::Volume;

sub assign_unique_material {
    my ($self) = @_;
    
    my $model = $self->object->model;
    my $material_id = 1 + $model->material_count;
    $self->material_id($material_id);
    return $model->set_material($material_id);
}

package Slic3r::Model::Instance;

sub transform_mesh {
    my ($self, $mesh, $dont_translate) = @_;
    
    $mesh->rotate($self->rotation, Slic3r::Point->new(0,0));   # rotate around mesh origin
    $mesh->scale($self->scaling_factor);                       # scale around mesh origin
    $mesh->translate(@{$self->offset}, 0) unless $dont_translate;
}

sub transform_polygon {
    my ($self, $polygon) = @_;
    
    $polygon->rotate($self->rotation, Slic3r::Point->new(0,0));   # rotate around origin
    $polygon->scale($self->scaling_factor);                       # scale around origin
}

1;
