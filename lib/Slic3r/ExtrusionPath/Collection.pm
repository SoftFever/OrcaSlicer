package Slic3r::ExtrusionPath::Collection;
use strict;
use warnings;

sub first_point {
    my $self = shift;
    return $self->[0]->[0];
}

# (Same algorithm as Polyline::Collection)
sub chained_path {
    my $self = shift;
    my ($start_near, $no_reverse) = @_;
    
    my @my_paths = @$self;
    return @my_paths if $self->no_sort;
    
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
            # path is reversed in place, but we got a copy from XS
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
    my @paths = map $_->split_at_acute_angles, @$self;
    $self->clear;
    $self->append(@paths);
}

sub detect_arcs {
    my $self = shift;
    
    return map $_->detect_arcs(@_), @$self;
}

1;
