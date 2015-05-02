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
    
    die "The supplied file couldn't be read because it's empty.\n"
        if $model->objects_count == 0;
    
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
        
        $new_object->set_name($args{name})
            if defined $args{name};
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

    $bb //= Slic3r::Geometry::BoundingBoxf->new;
    
    # we supply unscaled data to arrange()
    return @{Slic3r::Geometry::arrange(
        scalar(@$sizes),                # number of parts
        Slic3r::Pointf->new(
            max(map $_->x, @$sizes),        # cell width
            max(map $_->y, @$sizes),        # cell height  ,
        ),
        $distance,                      # distance between cells
        $bb,                            # bounding box of the area to fill (can be undef)
    )};
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
        
        if ($volume->material_id ne '') {
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
        
        $new_volume->set_name($args{name})
            if defined $args{name};
        $new_volume->set_material_id($args{material_id})
            if defined $args{material_id};
        $new_volume->set_modifier($args{modifier})
            if defined $args{modifier};
        $new_volume->config->apply($args{config})
            if defined $args{config};
    }
    
    if ($new_volume->material_id ne '' && !defined $self->model->get_material($new_volume->material_id)) {
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

1;
