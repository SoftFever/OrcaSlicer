package Slic3r::ExtrusionLoop;
use Moo;

# the underlying Slic3r::Polygon objects holds the geometry
has 'polygon' => (
    is          => 'rw',
    required    => 1,
    handles     => [qw(is_printable nearest_point_index_to reverse)],
);

has 'flow_spacing' => (is => 'rw');

# see EXTR_ROLE_* constants in ExtrusionPath.pm
has 'role'         => (is => 'rw', required => 1);

sub BUILD {
    my $self = shift;
    bless $self->polygon, 'Slic3r::Polygon';
    $self->polygon($self->polygon->serialize);
}

sub deserialize {
    my $self = shift;
    $self->polygon($self->polygon->deserialize);
}

sub shortest_path {
    my $self = shift;
    return $self;
}

sub split_at_index {
    my $self = shift;
    my ($index) = @_;

    $self->deserialize;

    my @new_points = ();
    push @new_points, @{$self->polygon}[$index .. $#{$self->polygon}];
    push @new_points, @{$self->polygon}[0 .. $index];
    
    return Slic3r::ExtrusionPath->new(
        polyline    => Slic3r::Polyline->new(\@new_points),
        role        => $self->role,
        flow_spacing => $self->flow_spacing,
    );
}

sub split_at {
    my $self = shift;
    my ($point) = @_;
    
    $point = Slic3r::Point->new($point);
    
    $self->deserialize;
    
    # find index of point
    my $i = -1;
    for (my $n = 0; $n <= $#{$self->polygon}; $n++) {
        if ($point->id eq $self->polygon->[$n]->id) {
            $i = $n;
            last;
        }
    }
    die "Point not found" if $i == -1;
    
    return $self->split_at_index($i);
}

sub split_at_first_point {
    my $self = shift;
    return $self->split_at_index(0);
}

# although a loop doesn't have endpoints, this method is provided to allow
# ExtrusionLoop objects to be added to an ExtrusionPath::Collection and
# sorted by the ->shortest_path() method
sub endpoints {
    my $self = shift;
    return ($self->polygon->[0], $self->polygon->[-1]);
}

# provided for ExtrusionPath::Collection->shortest_path()
sub points {
    my $self = shift;
    return $self->polygon;
}

1;
