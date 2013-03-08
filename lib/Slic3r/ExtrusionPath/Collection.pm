package Slic3r::ExtrusionPath::Collection;
use Moo;

has 'paths' => (is => 'rw', default => sub { [] });

sub endpoints {
    my $self = shift;
    return [ map $_->endpoints, @{$self->paths} ];
}

sub chained_path {
    my $self = shift;
    my ($start_near) = @_;
    
    # make sure we pass the same path objects to the Collection constructor
    # and the ->chained_path() method because the latter will reverse the
    # paths in-place when needed and we need to return them that way
    my @paths = map $_->unpack, @{$self->paths};
    my $collection = Slic3r::Polyline::Collection->new(
        polylines => [ map $_->polyline, @paths ],
    );
    
    return $collection->chained_path($start_near, \@paths);
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
