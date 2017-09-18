# extends C++ class Slic3r::Model
package Slic3r::Model;

use List::Util qw(first max any);

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

# Extends C++ class Slic3r::ModelMaterial
package Slic3r::Model::Material;

sub apply {
    my ($self, $attributes) = @_;
    $self->set_attribute($_, $attributes{$_}) for keys %$attributes;
}

# Extends C++ class Slic3r::ModelObject
package Slic3r::Model::Object;

use List::Util qw(first sum);

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

1;
