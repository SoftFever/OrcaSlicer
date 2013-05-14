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
    my $vertices = [];
    $mode eq 'ascii'
        ? _read_ascii($fh, $facets, $vertices)
        : _read_binary($fh, $facets, $vertices);
    close $fh;
    
    my $model = Slic3r::Model->new;
    my $object = $model->add_object(vertices => $vertices);
    my $volume = $object->add_volume(facets => $facets);
    return $model;
}

sub _read_ascii {
    my ($fh, $facets, $vertices) = @_;
    
    my $point_re = qr/(([^ ]+)\s+([^ ]+)\s+([^ ]+))/;
    
    my $facet;
    my %vertices_map = ();
    seek $fh, 0, 0;
    while (my $_ = <$fh>) {
        if (!$facet) {
            /^\s*facet\s+normal\s+/ or next;
            $facet = [];  # ignore normal
        } else {
            if (/^\s*endfacet/) {
                push @$facets, $facet;
                undef $facet;
            } else {
                /^\s*vertex\s+$point_re/o or next;
                my $vertex_id = $1;
                my $vertex_idx;
                if (exists $vertices_map{$vertex_id}) {
                    $vertex_idx = $vertices_map{$vertex_id};
                } else {
                    push @$vertices, [map $_ * 1, $2, $3, $4];
                    $vertex_idx = $vertices_map{$vertex_id} = $#$vertices;
                }
                push @$facet, $vertex_idx;
            }
        }
    }
    if ($facet) {
        die "STL file seems invalid\n";
    }
}

sub _read_binary {
    my ($fh, $facets, $vertices) = @_;
    
    die "bigfloat" unless length(pack "f", 1) == 4;
    
    my %vertices_map = ();
    binmode $fh;
    seek $fh, 80 + 4, 0;
    while (read $fh, my $_, 4*4*3+2) {
        push @$facets, my $facet = [];
        for (unpack 'x[f3](a[f3])3') {  # ignore normal
            my $vertex_idx;
            if (exists $vertices_map{$_}) {
                $vertex_idx = $vertices_map{$_};
            } else {
                push @$vertices, [ unpack 'f<3', $_ ];
                $vertex_idx = $vertices_map{$_} = $#$vertices;
            }
            push @$facet, $vertex_idx;
        }
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
