package Slic3r::Surface;
use Moo;

has 'expolygon' => (
    is => 'ro',
    required => 1,
    handles => [qw(encloses_point lines contour holes)],
);

has 'surface_type' => (
    is      => 'rw',
    #isa     => enum([qw(internal internal-solid bottom top)]),
);

# this integer represents the thickness of the surface expressed in layers
has 'depth_layers' => (is => 'ro', default => sub {1});

has 'bridge_angle' => (is => 'ro');

# static method to group surfaces having same surface_type, bridge_angle and depth_layers
sub group {
    my $class = shift;
    my $params = ref $_[0] eq 'HASH' ? shift(@_) : {};
    my (@surfaces) = @_;
    
    my %unique_types = ();
    foreach my $surface (@surfaces) {
        my $type = ($params->{merge_solid} && $surface->surface_type =~ /top|bottom|solid/)
            ? 'solid'
            : $surface->surface_type;
        $type .= "_" . ($surface->bridge_angle // ''); #/
        $type .= "_" . $surface->depth_layers;
        $unique_types{$type} ||= [];
        push @{ $unique_types{$type} }, $surface;
    }
    
    return values %unique_types;
}

sub add_hole {
    my $self = shift;
    my ($hole) = @_;
    
    push @$self, $hole;
}

sub id {
    my $self = shift;
    return $self->contour->id;
}

sub clipper_polygon {
    my $self = shift;
    
    return {
        outer => $self->contour->p,
        holes => [
            map $_->p, @{$self->holes}
        ],
    };
}

sub p {
    my $self = shift;
    return @{$self->expolygon};
}

1;
