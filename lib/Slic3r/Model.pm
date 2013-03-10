package Slic3r::Model;
use Moo;

use Slic3r::Geometry qw(X Y Z);

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
                $mesh->align_to_origin;
                $mesh->move(@{$instance->offset});
            }
            push @meshes, $mesh;
        }
    }
    
    return Slic3r::TriangleMesh->merge(@meshes);
}

package Slic3r::Model::Region;
use Moo;

has 'model'         => (is => 'ro', weak_ref => 1, required => 1);
has 'attributes'    => (is => 'rw', default => sub { {} });

package Slic3r::Model::Object;
use Moo;

use List::Util qw(first);
use Slic3r::Geometry qw(X Y Z);

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

sub scale {
    my $self = shift;
    my ($factor) = @_;
    return if $factor == 1;
    
    # transform vertex coordinates
    foreach my $vertex (@{$self->vertices}) {
        $vertex->[$_] *= $factor for X,Y,Z;
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
has 'rotation'  => (is => 'rw', default => sub { 0 });
has 'offset'    => (is => 'rw');

1;
