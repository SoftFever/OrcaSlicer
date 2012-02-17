package Slic3r::STL;
use Moo;

use Math::Clipper qw(integerize_coordinate_sets is_counter_clockwise);
use Slic3r::Geometry qw(X Y Z three_points_aligned longest_segment);
use XXX;

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    open my $fh, '<', $file or die "Failed to open $file\n";
    
    # let's detect whether file is ASCII or binary
    my $mode;
    {
        my $size = +(stat $fh)[7];
        $mode = 'ascii' if $size < 80 + 4;
        
        # skip binary header
        seek $fh, 80, 0;
        read $fh, my $buf, 4;
        my $triangle_count = unpack 'L', $buf;
        die "STL file seems invalid, could not read facet count\n" if !defined $triangle_count;
        my $expected_size =
            + 80 # header
            +  4 # count
            + $triangle_count * (
                + 4   # normal, pt,pt,pt (vectors)
                  * 4   # bytes per value
                  * 3   # values per vector
                + 2 # the trailing 'short'
            );
        $mode = ($size == $expected_size) ? 'binary' : 'ascii';
    }
    
    my $facets = [];
    $mode eq 'ascii'
        ? _read_ascii($fh, $facets)
        : _read_binary($fh, $facets);
    close $fh;
    
    my $vertices = [];
    {
        my %vertices_map = ();
        for (my $f = 0; $f <= $#$facets; $f++) {
            for (1..3) {
                my $point_id = join ',', @{$facets->[$f][$_]};
                if (exists $vertices_map{$point_id}) {
                    $facets->[$f][$_] = $vertices_map{$point_id};
                } else {
                    push @$vertices, $facets->[$f][$_];
                    $facets->[$f][$_] = $vertices_map{$point_id} = $#$vertices;
                }
            }
        }
    }
    
    return Slic3r::TriangleMesh->new(vertices => $vertices, facets => $facets);
}

sub _read_ascii {
    my ($fh, $facets) = @_;
    
    my $point_re = qr/([^ ]+)\s+([^ ]+)\s+([^ ]+)\s*$/;
    
    my $facet;
    seek $fh, 0, 0;
    while (<$fh>) {
        chomp;
        if (!$facet) {
            /^\s*facet\s+normal\s+$point_re/ or next;
            $facet = [ [$1, $2, $3] ];
        } else {
            if (/^\s*endfacet/) {
                push @$facets, $facet;
                undef $facet;
            } else {
                /^\s*vertex\s+$point_re/ or next;
                push @$facet, [$1, $2, $3];
            }
        }
    }
    if ($facet) {
        die "STL file seems invalid\n";
    }
}

sub _read_binary {
    my ($fh, $facets) = @_;
    
    die "bigfloat" unless length(pack "f", 1) == 4;
    
    binmode $fh;
    seek $fh, 80 + 4, 0;
    while (read $fh, $_, 4*4*3+2) {
        my @v = unpack '(f<3)4';
        push @$facets, [ [@v[0..2]], [@v[3..5]], [@v[6..8]], [@v[9..11]] ];
    }
}

sub write_file {
    my $self = shift;
    my ($file, $mesh, $binary) = @_;
    
    open my $fh, '>', $file;
    
    $binary
        ? _write_binary($fh, $mesh->facets)
        : _write_ascii($fh, $mesh->facets);
    
    close $fh;
}

sub _write_binary {
    my ($fh, $facets) = @_;
    
    die "bigfloat" unless length(pack "f", 1) == 4;
    
    binmode $fh;
    print $fh pack 'x80';
    print $fh pack 'L', ($#$facets + 1);
    print $fh pack '(f<3)4S', (map @$_, @$_), 0 for @$facets;
}

sub _write_ascii {
    my ($fh, $facets) = @_;
    
    printf $fh "solid\n";
    foreach my $facet (@$facets) {
        printf $fh "   facet normal %f %f %f\n", @{$facet->[0]};
        printf $fh "      outer loop\n";
        printf $fh "         vertex %f %f %f\n", @$_ for @$facet[1,2,3];
        printf $fh "      endloop\n";
        printf $fh "   endfacet\n";
    }
    printf $fh "endsolid\n";
}

1;
