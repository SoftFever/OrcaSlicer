package Slic3r::Format::STL;
use Moo;

use Slic3r::Geometry qw(X Y Z triangle_normal);

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    my $tmesh = Slic3r::TriangleMesh::XS->new;
    $tmesh->ReadSTLFile(Slic3r::encode_path($file));
    $tmesh->Repair;
    my ($vertices, $facets) = @{$tmesh->ToPerl};
    
    my $model = Slic3r::Model->new;
    my $object = $model->add_object(vertices => $vertices, mesh_stats => $tmesh->stats);
    my $volume = $object->add_volume(facets => $facets);
    return $model;
}

sub write_file {
    my $self = shift;
    my ($file, $model, %params) = @_;
    
    Slic3r::open(\my $fh, '>', $file);
    
    $params{binary}
        ? _write_binary($fh, $model->mesh)
        : _write_ascii($fh, $model->mesh);
    
    close $fh;
}

sub _write_binary {
    my ($fh, $mesh) = @_;
    
    die "bigfloat" unless length(pack "f", 1) == 4;
    
    binmode $fh;
    print $fh pack 'x80';
    print $fh pack 'L', scalar(@{$mesh->facets});
    foreach my $facet (@{$mesh->facets}) {
        print $fh pack '(f<3)4S',
            @{_facet_normal($mesh, $facet)},
            (map @{$mesh->vertices->[$_]}, @$facet[-3..-1]),
            0;
    }
}

sub _write_ascii {
    my ($fh, $mesh) = @_;
    
    printf $fh "solid\n";
    foreach my $facet (@{$mesh->facets}) {
        printf $fh "   facet normal %f %f %f\n", @{_facet_normal($mesh, $facet)};
        printf $fh "      outer loop\n";
        printf $fh "         vertex %f %f %f\n", @{$mesh->vertices->[$_]} for @$facet[-3..-1];
        printf $fh "      endloop\n";
        printf $fh "   endfacet\n";
    }
    printf $fh "endsolid\n";
}

sub _facet_normal {
    my ($mesh, $facet) = @_;
    return triangle_normal(map $mesh->vertices->[$_], @$facet[-3..-1]);
}

1;
