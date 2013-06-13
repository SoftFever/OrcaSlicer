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
    
    my $new_model = $class->new;
    foreach my $model (@models) {
        # merge material attributes (should we rename them in case of duplicates?)
        $new_model->set_material($_, { %{$model->materials->{$_}}, %{$model->materials->{$_} || {}} })
            for keys %{$model->materials};
        
        foreach my $object (@{$model->objects}) {
            my $new_object = $new_model->add_object(
                input_file          => $object->input_file,
                vertices            => $object->vertices,
                layer_height_ranges => $object->layer_height_ranges,
            );
            
            $new_object->add_volume(
                material_id         => $_->material_id,
                facets              => $_->facets,
            ) for @{$object->volumes};
            
            $new_object->add_instance(
                offset              => $_->offset,
                rotation            => $_->rotation,
            ) for @{ $object->instances // [] };
        }
    }
    
    return $new_model;
}

sub add_object {
    my $self = shift;
    
    my $object = Slic3r::Model::Object->new(model => $self, @_);
    push @{$self->objects}, $object;
    return $object;
}

sub set_material {
    my $self = shift;
    my ($material_id, $attributes) = @_;
    
    return $self->materials->{$material_id} = Slic3r::Model::Region->new(
        model       => $self,
        attributes  => $attributes || {},
    );
}

sub scale {
    my $self = shift;
    $_->scale(@_) for @{$self->objects};
}

sub arrange_objects {
    my $self = shift;
    my ($config) = @_;
    
    # do we have objects with no position?
    if (first { !defined $_->instances } @{$self->objects}) {
        # we shall redefine positions for all objects
        
        my ($copies, @positions) = $self->_arrange(
            config  => $config,
            items   => $self->objects,
        );
        
        # apply positions to objects
        foreach my $object (@{$self->objects}) {
            $object->align_to_origin;
            
            $object->instances([]);
            $object->add_instance(
                offset      => $_,
                rotation    => 0,
            ) for splice @positions, 0, $copies;
        }
        
    } else {
        # we only have objects with defined position
        
        # align the whole model to origin as it is
        $self->align_to_origin;
        
        # arrange this model as a whole
        my ($copies, @positions) = $self->_arrange(
            config  => $config,
            items   => [$self],
        );
        
        # apply positions to objects by translating the current positions
        foreach my $object (@{$self->objects}) {
            my @old_instances = @{$object->instances};
            $object->instances([]);
            foreach my $instance (@old_instances) {
                $object->add_instance(
                    offset      => $_,
                    rotation    => $instance->rotation,
                    scaling_factor => $instance->scaling_factor,
                ) for move_points($instance->offset, @positions);
            }
        }
    }
}

sub _arrange {
    my $self = shift;
    my %params = @_;
    
    my $config  = $params{config};
    my @items   = @{$params{items}};  # can be Model or Object objects, they have to implement size()
    
    if ($config->duplicate_grid->[X] > 1 || $config->duplicate_grid->[Y] > 1) {
        if (@items > 1) {
            die "Grid duplication is not supported with multiple objects\n";
        }
        my @positions = ();
        my $size = $items[0]->size;
        my $dist = $config->duplicate_distance;
        for my $x_copy (1..$config->duplicate_grid->[X]) {
            for my $y_copy (1..$config->duplicate_grid->[Y]) {
                push @positions, [
                    ($size->[X] + $dist) * ($x_copy-1),
                    ($size->[Y] + $dist) * ($y_copy-1),
                ];
            }
        }
        return ($config->duplicate_grid->[X] * $config->duplicate_grid->[Y]), @positions;
    } else {
        my $total_parts = $config->duplicate * @items;
        my $partx = max(map $_->size->[X], @items);
        my $party = max(map $_->size->[Y], @items);
        return $config->duplicate,
            Slic3r::Geometry::arrange
                ($total_parts, $partx, $party, (map $_, @{$config->bed_size}),
                $config->min_object_distance, $config);
    }
}

sub vertices {
    my $self = shift;
    return [ map @{$_->vertices}, @{$self->objects} ];
}

sub used_vertices {
    my $self = shift;
    return [ map @{$_->used_vertices}, @{$self->objects} ];
}

sub size {
    my $self = shift;
    return [ Slic3r::Geometry::size_3D($self->used_vertices) ];
}

sub extents {
    my $self = shift;
    return Slic3r::Geometry::bounding_box_3D($self->used_vertices);
}

sub align_to_origin {
    my $self = shift;
    
    # calculate the displacements needed to 
    # have lowest value for each axis at coordinate 0
    {
        my @extents = $self->extents;
        $self->move(map -$extents[$_][MIN], X,Y,Z);
    }
    
    # align all instances to 0,0 as well
    {
        my @instances = map @{$_->instances}, @{$self->objects};
        my @extents = Slic3r::Geometry::bounding_box_3D([ map $_->offset, @instances ]);
        $_->offset->translate(-$extents[X][MIN], -$extents[Y][MIN]) for @instances;
    }
}

sub move {
    my $self = shift;
    $_->move(@_) for @{$self->objects};
}

# flattens everything to a single mesh
sub mesh {
    my $self = shift;
    
    my @meshes = ();
    foreach my $object (@{$self->objects}) {
        my @instances = $object->instances ? @{$object->instances} : (undef);
        foreach my $instance (@instances) {
            my $mesh = $object->mesh->clone;
            if ($instance) {
                $mesh->rotate($instance->rotation);
                $mesh->scale($instance->scaling_factor);
                $mesh->align_to_origin;
                $mesh->move(@{$instance->offset});
            }
            push @meshes, $mesh;
        }
    }
    
    return Slic3r::TriangleMesh->merge(@meshes);
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
        foreach my $mesh ($volume->mesh->split_mesh) {
            my $new_object = $self->add_object(
                input_file          => $object->input_file,
                layer_height_ranges => $object->layer_height_ranges,
            );
            $new_object->add_volume(
                vertices    => $mesh->vertices,
                facets      => $mesh->facets,
                material_id => $volume->material_id,
            );
            
            # let's now align the new object to the origin and put its displacement
            # (extents) in the instances info
            my @extents = $mesh->extents;
            $new_object->align_to_origin;
            
            # add one instance per original instance applying the displacement
            $new_object->add_instance(
                offset      => [ $_->offset->[X] + $extents[X][MIN], $_->offset->[Y] + $extents[Y][MIN] ],
                rotation    => $_->rotation,
                scaling_factor => $_->scaling_factor,
            ) for @{ $object->instances // [] };
        }
    }
}

package Slic3r::Model::Region;
use Moo;

has 'model'         => (is => 'ro', weak_ref => 1, required => 1);
has 'attributes'    => (is => 'rw', default => sub { {} });

package Slic3r::Model::Object;
use Moo;

use List::Util qw(first);
use Slic3r::Geometry qw(X Y Z MIN MAX move_points move_points_3D);
use Storable qw(dclone);

has 'input_file' => (is => 'rw');
has 'model'     => (is => 'ro', weak_ref => 1, required => 1);
has 'vertices'  => (is => 'ro', default => sub { [] });
has 'volumes'   => (is => 'ro', default => sub { [] });
has 'instances' => (is => 'rw');
has 'layer_height_ranges' => (is => 'rw', default => sub { [] }); # [ z_min, z_max, layer_height ]

sub add_volume {
    my $self = shift;
    my %args = @_;
    
    if (my $vertices = delete $args{vertices}) {
        my $v_offset = @{$self->vertices};
        push @{$self->vertices}, @$vertices;
        
        @{$args{facets}} = map {
            my $f = [@$_];
            $f->[$_] += $v_offset for -3..-1;
            $f;
        } @{$args{facets}};
    }
    
    my $volume = Slic3r::Model::Volume->new(object => $self, %args);
    push @{$self->volumes}, $volume;
    return $volume;
}

sub add_instance {
    my $self = shift;
    
    $self->instances([]) if !defined $self->instances;
    push @{$self->instances}, Slic3r::Model::Instance->new(object => $self, @_);
    return $self->instances->[-1];
}

sub mesh {
    my $self = shift;
    
    # this mesh won't be suitable for check_manifoldness as multiple
    # facets from different volumes may use the same vertices
    return Slic3r::TriangleMesh->new(
        vertices => $self->vertices,
        facets   => [ map @{$_->facets}, @{$self->volumes} ],
    );
}

sub used_vertices {
    my $self = shift;
    return [ map $self->vertices->[$_], map @$_, map @{$_->facets}, @{$self->volumes} ];
}

sub size {
    my $self = shift;
    return [ Slic3r::Geometry::size_3D($self->used_vertices) ];
}

sub extents {
    my $self = shift;
    return Slic3r::Geometry::bounding_box_3D($self->used_vertices);
}

sub center {
    my $self = shift;
    
    my @extents = $self->extents;
    return [ map +($extents[$_][MAX] + $extents[$_][MIN])/2, X,Y,Z ];
}

sub bounding_box {
    my $self = shift;
    return Slic3r::Geometry::BoundingBox->new(extents => [ $self->extents ]);
}

sub align_to_origin {
    my $self = shift;
    
    # calculate the displacements needed to 
    # have lowest value for each axis at coordinate 0
    my @extents = $self->extents;
    my @shift = map -$extents[$_][MIN], X,Y,Z;
    $self->move(@shift);
    return @shift;
}

sub move {
    my $self = shift;
    @{$self->vertices} = move_points_3D([ @_ ], @{$self->vertices});
}

sub scale {
    my $self = shift;
    my ($factor) = @_;
    return if $factor == 1;
    
    # transform vertex coordinates
    foreach my $vertex (@{$self->vertices}) {
        $vertex->[$_] *= $factor for X,Y,Z;
    }
}

sub rotate {
    my $self = shift;
    my ($deg) = @_;
    return if $deg == 0;
    
    my $rad = Slic3r::Geometry::deg2rad($deg);
    
    # transform vertex coordinates
    foreach my $vertex (@{$self->vertices}) {
        @$vertex = (@{ +(Slic3r::Geometry::rotate_points($rad, undef, [ $vertex->[X], $vertex->[Y] ]))[0] }, $vertex->[Z]);
    }
}

sub materials_count {
    my $self = shift;
    
    my %materials = map { $_->material_id // '_default' => 1 } @{$self->volumes};
    return scalar keys %materials;
}

sub check_manifoldness {
    my $self = shift;
    return (first { !$_->mesh->check_manifoldness } @{$self->volumes}) ? 0 : 1;
}

sub clone { dclone($_[0]) }

package Slic3r::Model::Volume;
use Moo;

has 'object'        => (is => 'ro', weak_ref => 1, required => 1);
has 'material_id'   => (is => 'rw');
has 'facets'        => (is => 'rw', default => sub { [] });

sub mesh {
    my $self = shift;
    return Slic3r::TriangleMesh->new(
        vertices => $self->object->vertices,
        facets   => $self->facets,
    );
}

package Slic3r::Model::Instance;
use Moo;

has 'object'    => (is => 'ro', weak_ref => 1, required => 1);
has 'rotation'  => (is => 'rw', default => sub { 0 });  # around mesh center point
has 'scaling_factor' => (is => 'rw', default => sub { 1 });
has 'offset'    => (is => 'rw');  # must be Slic3r::Point object

1;
