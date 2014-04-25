package Slic3r::Format::STL;
use Moo;

use File::Basename qw(basename);

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    my $path = Slic3r::encode_path($file);
    die "Failed to open $file\n" if !-e $path;
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->ReadSTLFile($path);
    $mesh->repair;
    
    my $model = Slic3r::Model->new;
    
    my $material_id = basename($file);
    $model->set_material($material_id);
    my $object = $model->add_object;
    my $volume = $object->add_volume(mesh => $mesh, material_id => $material_id);
    return $model;
}

sub write_file {
    my $self = shift;
    my ($file, $mesh, %params) = @_;
    
    $mesh = $mesh->mesh if $mesh->isa('Slic3r::Model');
    
    my $path = Slic3r::encode_path($file);
    
    $params{binary}
        ? $mesh->write_binary($path)
        : $mesh->write_ascii($path);
}

1;
