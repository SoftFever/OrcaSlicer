package Slic3r::Format::OBJ;
use Moo;

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    open my $fh, '<', $file or die "Failed to open $file\n";
    my $vertices = [];
    my $facets = [];
    while (my $_ = <$fh>) {
        if (/^v ([^ ]+)\s+([^ ]+)\s+([^ ]+)/) {
            push @$vertices, [$1, $2, $3];
        } elsif (/^f (\d+).*? (\d+).*? (\d+).*?/) {
            push @$facets, [ $1-1, $2-1, $3-1 ];
        }
    }
    close $fh;
    
    return Slic3r::TriangleMesh->new(vertices => $vertices, facets => $facets);
}

1;
