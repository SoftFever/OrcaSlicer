package Slic3r::Format::STL;
use Moo;

use File::Basename qw(basename);

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->ReadSTLFile(Slic3r::encode_path($file));
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
    my ($file, $model, %params) = @_;
    
    my $path = Slic3r::encode_path($file);
    
    $params{binary}
        ? $model->mesh->write_binary($path)
        : $model->mesh->write_ascii($path);
}

1;
