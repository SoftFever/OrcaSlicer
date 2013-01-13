package Slic3r::Format::STL;
use Moo;

use Slic3r::Geometry qw(X Y Z triangle_normal);

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    Slic3r::open(\my $fh, '<', $file) or die "Failed to open $file\n";
    
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
        my %vertices_map = ();    # given a vertex's coordinates, what's its index?
        for (my $f = 0; $f <= $#$facets; $f++) {
            for (-3..-1) {
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
    
    my $model = Slic3r::Model->new;
    my $object = $model->add_object(vertices => $vertices);
    my $volume = $object->add_volume(facets => $facets);
    return $model;
}

sub _read_ascii {
    my ($fh, $facets) = @_;
    
    my $point_re = qr/([^ ]+)\s+([^ ]+)\s+([^ ]+)/;
    
    my $facet;
    seek $fh, 0, 0;
    while (my $_ = <$fh>) {
        if (!$facet) {
            /^\s*facet\s+normal\s+$point_re/ or next;
            $facet = [];  # ignore normal: [$1, $2, $3]
        } else {
            if (/^\s*endfacet/) {
                push @$facets, $facet;
                undef $facet;
            } else {
                /^\s*vertex\s+$point_re/o or next;
                push @$facet, [map $_ * 1, $1, $2, $3];
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
    while (read $fh, my $_, 4*4*3+2) {
        my @v = unpack '(f<3)4';
        push @$facets, [ [@v[3..5]], [@v[6..8]], [@v[9..11]] ];  # ignore normal: [@v[0..2]]
    }
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
