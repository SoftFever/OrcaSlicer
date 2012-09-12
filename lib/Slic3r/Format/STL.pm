package Slic3r::Format::STL;
use Moo;

use Slic3r::Geometry qw(X Y Z triangle_normal);

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
        my %vertices_map = ();    # given a vertex's coordinates, what's its index?
        my @vertices_facets = (); # given a vertex index, what are the indexes of its tangent facets?
        for (my $f = 0; $f <= $#$facets; $f++) {
            for (-3..-1) {
                my $point_id = join ',', @{$facets->[$f][$_]};
                if (exists $vertices_map{$point_id}) {
                    $facets->[$f][$_] = $vertices_map{$point_id};
                    ### push @{$vertices_facets[$facets->[$f][$_]]}, $f;
                } else {
                    push @$vertices, $facets->[$f][$_];
                    $facets->[$f][$_] = $vertices_map{$point_id} = $#$vertices;
                    ### $vertices_facets[$#$vertices] = [$f];
                }
            }
        }
        
        # The following loop checks that @vertices_facets only groups facets that
        # are really connected together (i.e. neighbors or sharing neighbors);
        # in other words it takes care of multiple vertices occupying the same
        # point in space. It enforces topological correctness which is needed by
        # the slicing algorithm.
        # I'm keeping it disabled until I find a good test case.
        # The two lines above commented out with '###' need to be
        # uncommented for this to work.
        if (0) {
            my $vertices_count = $#$vertices; # store it to avoid processing newly created vertices
            for (my $v = 0; $v <= $vertices_count; $v++) {
                my $more_than_one_vertex_in_this_point = 0;
                while (@{$vertices_facets[$v]}) {
                    my @facets_indexes = @{$vertices_facets[$v]};
                    @{$vertices_facets[$v]} = ();
                    
                    my @this_f = shift @facets_indexes;
                    CYCLE: while (@facets_indexes && @this_f) {
                        
                        # look for a facet that is connected to $this_f[-1] and whose common line contains $v
                        my @other_vertices_indexes = grep $_ != $v, @{$facets->[$this_f[-1]]}[-3..-1];
                        
                        OTHER: for my $other_f (@facets_indexes) {
                            # facet is connected if it shares one more point
                            for (grep $_ != $v, @{$facets->[$other_f]}[-3..-1]) {
                                if ($_ ~~ @other_vertices_indexes) {
                                    #printf "facet %d is connected to $other_f (sharing vertices $v and $_)\n", $this_f[-1];
                                    
                                    # TODO: we should ensure that the common edge has a different orientation
                                    # for each of the two adjacent facets
                                    
                                    push @this_f, $other_f;
                                    @facets_indexes = grep $_ != $other_f, @facets_indexes;
                                    next CYCLE;
                                }
                            }
                        }
                        # if we're here, then we couldn't find any facet connected to $this_f[-1]
                        # so we should move this one to a different cluster (that is, a new vertex)
                        # (or ignore it if it turns to be a non-manifold facet)
                        if (@this_f > 1) {
                            push @{$vertices_facets[$v]}, $this_f[-1];
                            pop @this_f;
                            $more_than_one_vertex_in_this_point++;
                        } else {
                            last CYCLE;
                        }
                    }
                    
                    if ($more_than_one_vertex_in_this_point) {
                        Slic3r::debugf "  more than one vertex in the same point\n";
                        push @$vertices, $vertices->[$v];
                        for my $f (@this_f) {
                            $facets->[$f][$_] = $#$vertices for grep $facets->[$f][$_] == $v, -3..-1;
                        }
                    }
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
    
    open my $fh, '>', $file;
    
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
