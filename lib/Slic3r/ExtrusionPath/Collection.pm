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
    
    my $collection = Slic3r::Polyline::Collection->new(
        polylines => [ map $_->unpack->polyline, @{$self->paths} ],
    );
    
    return $collection->shortest_path($start_near, $self->paths);
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
