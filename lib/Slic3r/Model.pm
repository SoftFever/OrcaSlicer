package Slic3r::Model;
use Moo;

use List::Util qw(first max);
use Slic3r::Geometry qw(X Y Z MIN move_points);

has 'materials' => (is => 'ro', default => sub { {} });
has 'objects'   => (is => 'ro', default => sub { [] });

sub read_from_file {
    my $class = shift;
    my ($input_file) = @_;
    
    my $model = $input_file =~ /\.stl$/i            ? Slic3r::Format::STL->read_file($input_file)
              : $input_file =~ /\.obj$/i            ? Slic3r::Format::OBJ->read_file($input_file)
              : $input_file =~ /\.amf(\.xml)?$/i    ? Slic3r::Format::AMF->read_file($input_file)
              : die "Input file must have .stl, .obj or .amf(.xml) extension\n";
    
    $_->input_file($input_file) for @{$model->objects};
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
    
    my $new_object;
    if (@_ == 1) {
        # we have a Model::Object
        my ($object) = @_;
        
        $new_object = $self->add_object(
            input_file          => $object->input_file,
            config              => $object->config,
            layer_height_ranges => $object->layer_height_ranges,    # TODO: clone!
        );
        
        foreach my $volume (@{$object->volumes}) {
            $new_object->add_volume($volume);
        }
        
        $new_object->add_instance(
            offset              => $_->offset,
            rotation            => $_->rotation,
            scaling_factor      => $_->scaling_factor,
        ) for @{ $object->instances // [] };
    } else {
        push @{$self->objects}, $new_object = Slic3r::Model::Object->new(model => $self, @_);
    }
    
    return $new_object;
}

sub delete_object {
    my ($self, $obj_idx) = @_;
    splice @{$self->objects}, $obj_idx, 1;
}

sub delete_all_objects {
    my ($self) = @_;
    @{$self->objects} = ();
}

sub set_material {
    my $self = shift;
    my ($material_id, $attributes) = @_;
    
    return $self->materials->{$material_id} = Slic3r::Model::Material->new(
        model       => $self,
        attributes  => $attributes || {},
    );
}

sub duplicate_objects_grid {
    my ($self, $grid, $distance) = @_;
    
    die "Grid duplication is not supported with multiple objects\n"
        if @{$self->objects} > 1;
    
    my $object = $self->objects->[0];
    @{$object->instances} = ();
    
    my $size = $object->bounding_box->size;
    for my $x_copy (1..$grid->[X]) {
        for my $y_copy (1..$grid->[Y]) {
            $object->add_instance(
                offset => [
                    ($size->[X] + $distance) * ($x_copy-1),
                    ($size->[Y] + $distance) * ($y_copy-1),
                ],
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
            ### $object->add_instance($instance->clone);  if we had clone()
            $object->add_instance(
                offset          => [ @{$instance->offset} ],
                rotation        => $instance->rotation,
                scaling_factor  => $instance->scaling_factor,
            ) for 2..$copies_num;
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
        $_->offset([ @{shift @positions} ]) for @{$object->instances};
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
                ### $object->add_instance($instance->clone);  if we had clone()
                $object->add_instance(
                    offset          => [ $instance->offset->[X] + $pos->[X], $instance->offset->[Y] + $pos->[Y] ],
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
            $instance->offset->[X] += $shift[X];
            $instance->offset->[Y] += $shift[Y];
        }
        $object->update_bounding_box;
    }
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

# this method splits objects into multiple distinct objects by walking their meshes
sub split_meshes {
    my $self = shift;
    
    my @objects = @{$self->objects};
    @{$self->objects} = ();
    
    foreach my $object (@objects) {
        if (@{$object->volumes} > 1) {
            # We can't split meshes if there's more than one material, because
            # we can't group the resulting meshes by object afterwards
            push @{$self->objects}, $object;
            next;
        }
        
        my $volume = $object->volumes->[0];
        foreach my $mesh (@{$volume->mesh->split}) {
            my $new_object = $self->add_object(
                input_file          => $object->input_file,
                config              => $object->config->clone,
                layer_height_ranges => $object->layer_height_ranges,   # TODO: this needs to be cloned
            );
            $new_object->add_volume(
                mesh        => $mesh,
                material_id => $volume->material_id,
            );
            
            # add one instance per original instance
            $new_object->add_instance(
                offset          => [ @{$_->offset} ],
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
    if (exists $self->materials->{$material_id}) {
        $name //= $self->materials->{$material_id}->attributes->{$_} for qw(Name name);
    }
    $name //= $material_id;
    return $name;
}

package Slic3r::Model::Material;
use Moo;

has 'model'         => (is => 'ro', weak_ref => 1, required => 1);
has 'attributes'    => (is => 'rw', default => sub { {} });
has 'config'        => (is => 'rw', default => sub { Slic3r::Config->new });

package Slic3r::Model::Object;
use Moo;

use File::Basename qw(basename);
use List::Util qw(first sum);
use Slic3r::Geometry qw(X Y Z MIN MAX);

has 'input_file'            => (is => 'rw');
has 'model'                 => (is => 'ro', weak_ref => 1, required => 1);
has 'volumes'               => (is => 'ro', default => sub { [] });
has 'instances'             => (is => 'rw');
has 'config'                => (is => 'rw', default => sub { Slic3r::Config->new });
has 'layer_height_ranges'   => (is => 'rw', default => sub { [] }); # [ z_min, z_max, layer_height ]
has '_bounding_box'         => (is => 'rw');

sub add_volume {
    my $self = shift;
    
    my $new_volume;
    if (@_ == 1) {
        # we have a Model::Volume
        my ($volume) = @_;
        
        $new_volume = Slic3r::Model::Volume->new(
            object      => $self,
            material_id => $volume->material_id,
            mesh        => $volume->mesh->clone,
            modifier    => $volume->modifier,
        );
        
        if (defined $volume->material_id) {
            #  merge material attributes and config (should we rename materials in case of duplicates?)
            if (my $material = $volume->object->model->materials->{$volume->material_id}) {
                my %attributes = %{ $material->attributes };
                if (exists $self->model->materials->{$volume->material_id}) {
                    %attributes = (%attributes, %{ $self->model->materials->{$volume->material_id}->attributes })
                }
                my $new_material = $self->model->set_material($volume->material_id, {%attributes});
                $new_material->config->apply($material->config);
            }
        }
    } else {
        my %args = @_;
        $new_volume = Slic3r::Model::Volume->new(
            object => $self,
            %args,
        );
    }
    
    push @{$self->volumes}, $new_volume;
    
    # invalidate cached bounding box
    $self->_bounding_box(undef);
    
    return $new_volume;
}

sub delete_volume {
    my ($self, $i) = @_;
    splice @{$self->volumes}, $i, 1;
}

sub add_instance {
    my $self = shift;
    my %params = @_;
    
    $self->instances([]) if !defined $self->instances;
    push @{$self->instances}, my $i = Slic3r::Model::Instance->new(object => $self, %params);
    $self->_bounding_box(undef);
    return $i;
}

sub delete_last_instance {
    my ($self) = @_;
    pop @{$self->instances};
    $self->_bounding_box(undef);
}

sub instances_count {
    my $self = shift;
    return scalar(@{ $self->instances // [] });
}

sub raw_mesh {
    my $self = shift;
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->merge($_->mesh) for grep !$_->modifier, @{ $self->volumes };
    return $mesh;
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
    
    if (defined $self->instances) {
        foreach my $instance (@{ $self->instances }) {
            $instance->offset->[X] -= $shift[X];
            $instance->offset->[Y] -= $shift[Y];
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

package Slic3r::Model::Volume;
use Moo;

has 'object'            => (is => 'ro', weak_ref => 1, required => 1);
has 'material_id'       => (is => 'rw');
has 'mesh'              => (is => 'rw', required => 1);
has 'modifier'          => (is => 'rw', defualt => sub { 0 });

sub assign_unique_material {
    my ($self) = @_;
    
    my $model = $self->object->model;
    my $material_id = 1 + scalar keys %{$model->materials};
    $self->material_id($material_id);
    return $model->set_material($material_id);
}

package Slic3r::Model::Instance;
use Moo;

has 'object'            => (is => 'ro', weak_ref => 1, required => 1);
has 'rotation'          => (is => 'rw', default => sub { 0 });  # around mesh center point
has 'scaling_factor'    => (is => 'rw', default => sub { 1 });
has 'offset'            => (is => 'rw');  # must be arrayref in *unscaled* coordinates

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
