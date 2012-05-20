package Slic3r::Format::AMF;
use Moo;

use Slic3r::Geometry qw(X Y Z);

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    eval "require Slic3r::Format::AMF::Parser; 1"
        or die "AMF parsing requires XML::SAX\n";
    
    open my $fh, '<', $file or die "Failed to open $file\n";
    
    my $vertices = [];
    my $materials = {};
    my $meshes_by_material = {};
    XML::SAX::PurePerl
        ->new(Handler => Slic3r::Format::AMF::Parser->new(
            _vertices           => $vertices,
            _materials          => $materials,
            _meshes_by_material => $meshes_by_material,
         ))
        ->parse_file($fh);
    
    close $fh;
    
    $_ = Slic3r::TriangleMesh->new(vertices => $vertices, facets => $_)
        for values %$meshes_by_material;
    
    return $materials, $meshes_by_material;
}

sub write_file {
    my $self = shift;
    my ($file, $materials, $meshes_by_material) = @_;
    
    my %vertices_offset = ();
    
    open my $fh, '>', $file;
    binmode $fh, ':utf8';
    printf $fh qq{<?xml version="1.0" encoding="UTF-8"?>\n};
    printf $fh qq{<amf unit="millimeter">\n};
    printf $fh qq{  <metadata type="cad">Slic3r %s</metadata>\n}, $Slic3r::VERSION;
    foreach my $material_id (keys %$materials) {
        printf $fh qq{  <material id="%s">\n}, $material_id;
        for (keys %{$materials->{$material_id}}) {
             printf $fh qq{    <metadata type=\"%s\">%s</metadata>\n}, $_, $materials->{$material_id}{$_};
        }
        printf $fh qq{  </material>\n};
    }
    printf $fh qq{  <object id="0">\n};
    printf $fh qq{    <mesh>\n};
    printf $fh qq{      <vertices>\n};
    my $vertices_count = 0;
    foreach my $mesh (values %$meshes_by_material) {
        $vertices_offset{$mesh} = $vertices_count;
        foreach my $vertex (@{$mesh->vertices}, ) {
            printf $fh qq{        <vertex>\n};
            printf $fh qq{          <coordinates>\n};
            printf $fh qq{            <x>%s</x>\n}, $vertex->[X];
            printf $fh qq{            <y>%s</y>\n}, $vertex->[Y];
            printf $fh qq{            <z>%s</z>\n}, $vertex->[Z];
            printf $fh qq{          </coordinates>\n};
            printf $fh qq{        </vertex>\n};
            $vertices_count++;
        }
    }
    printf $fh qq{      </vertices>\n};
    foreach my $material_id (sort keys %$meshes_by_material) {
        my $mesh = $meshes_by_material->{$material_id};
        printf $fh qq{      <volume%s>\n},
            ($material_id eq '_') ? '' : " materialid=\"$material_id\"";
        foreach my $facet (@{$mesh->facets}) {
            printf $fh qq{        <triangle>\n};
            printf $fh qq{          <v%d>%d</v%d>\n}, $_, $facet->[$_] + $vertices_offset{$mesh}, $_
                for -3..-1;
            printf $fh qq{        </triangle>\n};
        }
        printf $fh qq{      </volume>\n};
    }
    printf $fh qq{    </mesh>\n};
    printf $fh qq{  </object>\n};
    printf $fh qq{</amf>\n};
    close $fh;
}

1;
