package Slic3r::AMF;
use Moo;
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

1;
