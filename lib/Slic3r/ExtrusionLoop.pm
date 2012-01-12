package Slic3r::ExtrusionLoop;
use Moo;

use XXX;

# the underlying Slic3r::Polygon objects holds the geometry
has 'polygon' => (
    is          => 'ro',
    required    => 1,
    handles     => [qw(is_printable nearest_point_to)],
);

# perimeter/fill/solid-fill/bridge/skirt
has 'role'         => (is => 'rw', required => 1);

sub BUILD {
    my $self = shift;
    bless $self->polygon, 'Slic3r::Polygon';
}

sub split_at {
    my $self = shift;
    my ($point) = @_;
    
    $point = Slic3r::Point->new($point);
    
    # find index of point
    my $i = -1;
    for (my $n = 0; $n <= $#{$self->polygon}; $n++) {
        if ($point->id eq $self->polygon->[$n]->id) {
            $i = $n;
            last;
        }
    }
    die "Point not found" if $i == -1;
    
    my @new_points = ();
    push @new_points, @{$self->polygon}[$i .. $#{$self->polygon}];
    push @new_points, @{$self->polygon}[0 .. $i];
    
    return Slic3r::ExtrusionPath->new(
        polyline    => Slic3r::Polyline->new(\@new_points),
        role        => $self->role,
    );
}

1;
