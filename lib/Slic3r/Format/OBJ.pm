package Slic3r::Format::OBJ;
use Moo;

use File::Basename qw(basename);

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    Slic3r::open(\my $fh, '<', $file) or die "Failed to open $file\n";
    my $vertices = [];
    my $facets = [];
    while (<$fh>) {
        if (/^v ([^ ]+)\s+([^ ]+)\s+([^ ]+)/) {
            push @$vertices, [$1, $2, $3];
        } elsif (/^f (\d+).*? (\d+).*? (\d+).*?/) {
            push @$facets, [ $1-1, $2-1, $3-1 ];
        }
    }
    close $fh;
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->ReadFromPerl($vertices, $facets);
    $mesh->repair;
    
    my $model = Slic3r::Model->new;
    
    my $basename = basename($file);
    my $object = $model->add_object(input_file => $file, name => $basename);
    my $volume = $object->add_volume(mesh => $mesh, name => $basename);
    return $model;
}

1;
