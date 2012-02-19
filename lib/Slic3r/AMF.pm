package Slic3r::AMF;
use Moo;

use Slic3r::Geometry qw(X Y Z);
use XXX;

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    eval "require Slic3r::AMF::Parser; 1"
        or die "AMF parsing requires XML::SAX::ExpatXS\n";
    
    open my $fh, '<', $file or die "Failed to open $file\n";
    
    my $vertices = [];
    my $facets = [];
    XML::SAX::ExpatXS
        ->new(Handler => Slic3r::AMF::Parser->new(
            _vertices    => $vertices,
            _facets      => $facets,
         ))
        ->parse_file($fh);
    
    close $fh;
    
    return Slic3r::TriangleMesh->new(vertices => $vertices, facets => $facets);
}

sub write_file {
    my $self = shift;
    my ($file, $mesh) = @_;
    
    open my $fh, '>', $file;
    binmode $fh, ':utf8';
    
    printf $fh qq{<?xml version="1.0" encoding="UTF-8"?>\n};
    printf $fh qq{<amf unit="millimeter">\n};
    printf $fh qq{  <metadata type="cad">Slic3r %s</metadata>\n}, $Slic3r::VERSION;
    printf $fh qq{  <object id="0">\n};
    printf $fh qq{    <mesh>\n};
    printf $fh qq{      <vertices>\n};
    foreach my $vertex (@{$mesh->vertices}) {
        printf $fh qq{        <vertex>\n};
        printf $fh qq{          <coordinates>\n};
        printf $fh qq{            <x>%s</x>\n}, $vertex->[X];
        printf $fh qq{            <y>%s</y>\n}, $vertex->[Y];
        printf $fh qq{            <z>%s</z>\n}, $vertex->[Z];
        printf $fh qq{          </coordinates>\n};
        printf $fh qq{        </vertex>\n};
    }
    printf $fh qq{      </vertices>\n};
    printf $fh qq{      <volume>\n};
    foreach my $facet (@{$mesh->facets}) {
        printf $fh qq{        <triangle>\n};
        printf $fh qq{          <v%d>%d</v%d>\n}, $_, $facet->[$_], $_ for 1..3;
        printf $fh qq{        </triangle>\n};
    }
    printf $fh qq{      </volume>\n};
    printf $fh qq{    </mesh>\n};
    printf $fh qq{  </object>\n};
    printf $fh qq{</amf>\n};
    
    close $fh;
}

1;
