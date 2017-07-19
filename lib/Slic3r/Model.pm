# extends C++ class Slic3r::Model
package Slic3r::Model;

use File::Basename qw(basename);
use List::Util qw(first max any);

sub read_from_file {
    my ($class, $input_file, $add_default_instances) = @_;
    $add_default_instances //= 1;
    
    my $model = $input_file =~ /\.[sS][tT][lL]$/                    ? Slic3r::Model->load_stl(Slic3r::encode_path($input_file), basename($input_file))
              : $input_file =~ /\.[oO][bB][jJ]$/                    ? Slic3r::Model->load_obj(Slic3r::encode_path($input_file), basename($input_file))
              : $input_file =~ /\.[aA][mM][fF](\.[xX][mM][lL])?$/   ? Slic3r::Model->load_amf(Slic3r::encode_path($input_file))
              : $input_file =~ /\.[pP][rR][uU][sS][aA]$/            ? Slic3r::Model->load_prus(Slic3r::encode_path($input_file))
              : die "Input file must have .stl, .obj or .amf(.xml) extension\n";
    
    die "The supplied file couldn't be read because it's empty.\n"
        if $model->objects_count == 0;
    
    $_->set_input_file($input_file) for @{$model->objects};
    $model->add_default_instances if $add_default_instances;
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

sub print_info {
    my $self = shift;
    $_->print_info for @{$self->objects};
}

sub looks_like_multipart_object {
    my ($self) = @_;
    
    return 0 if $self->objects_count == 1;
    return 0 if any { $_->volumes_count > 1 } @{$self->objects};
    return 0 if any { @{$_->config->get_keys} > 1 } @{$self->objects};
    
    my %heights = map { $_ => 1 } map $_->mesh->bounding_box->z_min, map @{$_->volumes}, @{$self->objects};
    return scalar(keys %heights) > 1;
}

sub convert_multipart_object {
    my ($self) = @_;
    
    my @objects = @{$self->objects};
    my $object = $self->add_object(
        input_file          => $objects[0]->input_file,
    );
    foreach my $v (map @{$_->volumes}, @objects) {
        my $volume = $object->add_volume($v);
        $volume->set_name($v->object->name);
    }
    $object->add_instance($_) for map @{$_->instances}, @objects;
    
    $self->delete_object($_) for reverse 0..($self->objects_count-2);
}

# Extends C++ class Slic3r::ModelMaterial
package Slic3r::Model::Material;

sub apply {
    my ($self, $attributes) = @_;
    $self->set_attribute($_, $attributes{$_}) for keys %$attributes;
}

# Extends C++ class Slic3r::ModelObject
package Slic3r::Model::Object;

use File::Basename qw(basename);
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
