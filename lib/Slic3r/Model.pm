package Slic3r::Model;
use Moo;

use List::Util qw(first max);
use Slic3r::Geometry qw(X Y Z MIN move_points);

has 'materials' => (is => 'ro', default => sub { {} });
has 'objects'   => (is => 'ro', default => sub { [] });
has '_bounding_box' => (is => 'rw');

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
    foreach my $model (@models) {
        # merge material attributes (should we rename them in case of duplicates?)
        $new_model->set_material($_, { %{$model->materials->{$_}}, %{$model->materials->{$_} || {}} })
            for keys %{$model->materials};
        
        foreach my $object (@{$model->objects}) {
            my $new_object = $new_model->add_object(
                input_file          => $object->input_file,
                config              => $object->config,
                layer_height_ranges => $object->layer_height_ranges,
            );
            
            $new_object->add_volume(
                material_id         => $_->material_id,
                mesh                => $_->mesh->clone,
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
    $self->_bounding_box(undef);
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
        my @sizes = map $_->size, @items;
        my $partx = max(map $_->[X], @sizes);
        my $party = max(map $_->[Y], @sizes);
        return $config->duplicate,
            Slic3r::Geometry::arrange
                ($total_parts, $partx, $party, (map $_, @{$config->bed_size}),
                $config->min_object_distance, $config);
    }
}

sub size {
    my $self = shift;
    return $self->bounding_box->size;
}

sub bounding_box {
    my $self = shift;
    
    if (!defined $self->_bounding_box) {
        $self->_bounding_box(Slic3r::Geometry::BoundingBox->merge(map $_->bounding_box, @{$self->objects}));
    }
    return $self->_bounding_box;
}

sub align_to_origin {
    my $self = shift;
    
    # calculate the displacements needed to 
    # have lowest value for each axis at coordinate 0
    {
        my $bb = $self->bounding_box;
        $self->move(map -$bb->extents->[$_][MIN], X,Y,Z);
    }
    
    # align all instances to 0,0 as well
    {
        my @instances = map @{$_->instances}, @{$self->objects};
        my @extents = Slic3r::Geometry::bounding_box_3D([ map $_->offset, @instances ]);
        $_->offset->translate(-$extents[X][MIN], -$extents[Y][MIN]) for @instances;
    }
}

sub scale {
    my $self = shift;
    $_->scale(@_) for @{$self->objects};
    $self->_bounding_box->scale(@_) if defined $self->_bounding_box;
}

sub move {
    my $self = shift;
    my @shift = @_;
    
    $_->move(@shift) for @{$self->objects};
    $self->_bounding_box->translate(@shift) if defined $self->_bounding_box;
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
                $mesh->rotate($instance->rotation, Slic3r::Point->new(0,0));
                $mesh->scale($instance->scaling_factor);
                $mesh->align_to_origin;
                $mesh->translate(@{$instance->offset}, 0);
            }
            push @meshes, $mesh;
        }
    }
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->merge($_) for @meshes;
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
                config              => $object->config,
                layer_height_ranges => $object->layer_height_ranges,
            );
            $new_object->add_volume(
                mesh        => $mesh,
                material_id => $volume->material_id,
            );
            
            # let's now align the new object to the origin and put its displacement
            # (extents) in the instances info
            my $bb = $mesh->bounding_box;
            $new_object->align_to_origin;
            
            # add one instance per original instance applying the displacement
            $new_object->add_instance(
                offset      => [ $_->offset->[X] + $bb->x_min, $_->offset->[Y] + $bb->y_min ],
                rotation    => $_->rotation,
                scaling_factor => $_->scaling_factor,
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
    } elsif ($material_id eq '_') {
        $name = 'Default material';
    }
    $name //= $material_id;
    return $name;
}

package Slic3r::Model::Region;
use Moo;

has 'model'         => (is => 'ro', weak_ref => 1, required => 1);
has 'attributes'    => (is => 'rw', default => sub { {} });

package Slic3r::Model::Object;
use Moo;

use File::Basename qw(basename);
use List::Util qw(first sum);
use Slic3r::Geometry qw(X Y Z MIN MAX move_points move_points_3D);
use Storable qw(dclone);

has 'input_file' => (is => 'rw');
has 'model'     => (is => 'ro', weak_ref => 1, required => 1);
has 'volumes'   => (is => 'ro', default => sub { [] });
has 'instances' => (is => 'rw');
has 'config'    => (is => 'rw', default => sub { Slic3r::Config->new });
has 'layer_height_ranges' => (is => 'rw', default => sub { [] }); # [ z_min, z_max, layer_height ]
has 'material_mapping'      => (is => 'rw', default => sub { {} }); # { material_id => extruder_idx }
has '_bounding_box' => (is => 'rw');

sub add_volume {
    my $self = shift;
    my %args = @_;
    
    push @{$self->volumes}, my $volume = Slic3r::Model::Volume->new(
        object => $self,
        %args,
    );
    $self->_bounding_box(undef);
    $self->model->_bounding_box(undef);
    return $volume;
}

sub add_instance {
    my $self = shift;
    
    $self->instances([]) if !defined $self->instances;
    push @{$self->instances}, Slic3r::Model::Instance->new(object => $self, @_);
    $self->model->_bounding_box(undef);
    return $self->instances->[-1];
}

sub mesh {
    my $self = shift;
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->merge($_->mesh) for @{$self->volumes};
    return $mesh;
}

sub size {
    my $self = shift;
    return $self->bounding_box->size;
}

sub center {
    my $self = shift;
    return $self->bounding_box->center;
}

sub center_2D {
    my $self = shift;
    return $self->bounding_box->center_2D;
}

sub bounding_box {
    my $self = shift;
    
    if (!defined $self->_bounding_box) {
        my @meshes = map $_->mesh, @{$self->volumes};
        my $bounding_box = Slic3r::Geometry::BoundingBox->new_from_bb((shift @meshes)->bb3);
        $bounding_box->merge(Slic3r::Geometry::BoundingBox->new_from_bb($_->bb3)) for @meshes;
        $self->_bounding_box($bounding_box);
    }
    return $self->_bounding_box;
}

sub align_to_origin {
    my $self = shift;
    
    # calculate the displacements needed to 
    # have lowest value for each axis at coordinate 0
    my $bb = $self->bounding_box;
    my @shift = map -$bb->extents->[$_][MIN], X,Y,Z;
    $self->move(@shift);
    return @shift;
}

sub move {
    my $self = shift;
    my @shift = @_;
    
    $_->mesh->translate(@shift) for @{$self->volumes};
    $self->_bounding_box->translate(@shift) if defined $self->_bounding_box;
}

sub scale {
    my $self = shift;
    my ($factor) = @_;
    return if $factor == 1;
    
    $_->mesh->scale($factor) for @{$self->volumes};
    $self->_bounding_box->scale($factor) if defined $self->_bounding_box;
}

sub rotate {
    my $self = shift;
    my ($deg) = @_;
    return if $deg == 0;
    
    $_->mesh->rotate($deg, Slic3r::Point->(0,0)) for @{$self->volumes};
    $self->_bounding_box(undef);
}

sub materials_count {
    my $self = shift;
    
    my %materials = map { $_->material_id // '_default' => 1 } @{$self->volumes};
    return scalar keys %materials;
}

sub unique_materials {
    my $self = shift;
    
    my %materials = ();
    $materials{ $_->material_id // '_' } = 1 for @{$self->volumes};
    return sort keys %materials;
}

sub facets_count {
    my $self = shift;
    return sum(map $_->facets_count, @{$self->volumes});
}

sub needed_repair {
    my $self = shift;
    return (first { !$_->mesh->needed_repair } @{$self->volumes}) ? 0 : 1;
}

sub mesh_stats {
    my $self = shift;
    
    # TODO: sum values from all volumes
    return $self->volumes->[0]->mesh->stats;
}

sub print_info {
    my $self = shift;
    
    printf "Info about %s:\n", basename($self->input_file);
        printf "  size:              x=%.3f y=%.3f z=%.3f\n", @{$self->size};
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
        printf "  number of facets:  %d\n", scalar(map @{$_->facets}, @{$self->volumes});
    }
}

sub clone { dclone($_[0]) }

package Slic3r::Model::Volume;
use Moo;

has 'object'        => (is => 'ro', weak_ref => 1, required => 1);
has 'material_id'   => (is => 'rw');
has 'mesh'          => (is => 'rw', required => 1);

package Slic3r::Model::Instance;
use Moo;

has 'object'    => (is => 'ro', weak_ref => 1, required => 1);
has 'rotation'  => (is => 'rw', default => sub { 0 });  # around mesh center point
has 'scaling_factor' => (is => 'rw', default => sub { 1 });
has 'offset'    => (is => 'rw');  # must be Slic3r::Point object

1;
