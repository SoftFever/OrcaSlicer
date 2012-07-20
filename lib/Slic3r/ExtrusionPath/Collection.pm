package Slic3r::ExtrusionPath::Collection;
use Moo;

has 'paths' => (is => 'rw', default => sub { [] });

sub endpoints {
    my $self = shift;
    return [ map $_->endpoints, @{$self->paths} ];
}

sub shortest_path {
    my $self = shift;
    my ($start_near) = @_;
    
    my @my_paths = map $_->unpack, @{$self->paths};
    
    my @paths = ();
    my $start_at;
    my $endpoints = [ map $_->endpoints, @my_paths ];
    while (@my_paths) {
        # find nearest point
        my $start_index = $start_near
            ? Slic3r::Geometry::nearest_point_index($start_near, $endpoints)
            : 0;

        my $path_index = int($start_index/2);
        if ($start_index%2) { # index is end so reverse to make it the start
            $my_paths[$path_index]->reverse;
        }
        push @paths, splice @my_paths, $path_index, 1;
        splice @$endpoints, $path_index*2, 2;
        $start_near = $paths[-1]->points->[-1];
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
