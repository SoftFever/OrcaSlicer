package Slic3r::ExtrusionPath::Collection;
use Moo;

has 'paths' => (is => 'rw', default => sub { [] });
has 'no_sort' => (is => 'rw');

# no-op
sub unpack { $_[0] }

sub first_point {
    my $self = shift;
    return $self->paths->[0]->polyline->[0];
}

# Note that our paths will be reversed in place when necessary.
# (Same algorithm as Polyline::Collection)
sub chained_path {
    my $self = shift;
    my ($start_near, $no_reverse) = @_;
    
    return @{$self->paths} if $self->no_sort;
    my @my_paths = @{$self->paths};
    
    my @paths = ();
    my $start_at;
    my $endpoints = $no_reverse
        ? [ map { @$_[0,0] }  @my_paths ]
        : [ map { @$_[0,-1] } @my_paths ];
    while (@my_paths) {
        # find nearest point
        my $start_index = defined $start_near
            ? Slic3r::Geometry::nearest_point_index($start_near, $endpoints)
            : 0;

        my $path_index = int($start_index/2);
        if ($start_index % 2 && !$no_reverse) { # index is end so reverse to make it the start
            $my_paths[$path_index]->reverse;
        }
        push @paths, splice @my_paths, $path_index, 1;
        splice @$endpoints, $path_index*2, 2;
        $start_near = $paths[-1][-1];
    }
    return @paths;
}

sub cleanup {
    my $self = shift;
    
    # split paths at angles that are too acute to be printed as they will cause blobs
    @{$self->paths} = map $_->split_at_acute_angles, @{$self->paths};
}

sub detect_arcs {
    my $self = shift;
    @{$self->paths} = map $_->detect_arcs(@_), @{$self->paths};
}

1;
