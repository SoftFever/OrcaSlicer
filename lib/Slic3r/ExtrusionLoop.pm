package Slic3r::ExtrusionLoop;
use Moo;

use Slic3r::Geometry qw(same_point);

# the underlying Slic3r::Polygon objects holds the geometry
has 'polygon' => (
    is          => 'rw',
    required    => 1,
    handles     => [qw(is_printable nearest_point_index_to reverse)],
);

has 'flow_spacing' => (is => 'rw', required => 1);

# see EXTR_ROLE_* constants in ExtrusionPath.pm
has 'role'         => (is => 'rw', required => 1);

use constant PACK_FMT => 'fca*';

# class or object method
sub pack {
    my $self = shift;
    my %args = @_;
    
    if (ref $self) {
        %args = map { $_ => $self->$_ } qw(flow_spacing role polygon);
    }
    
    my $o = \ pack PACK_FMT,
        $args{flow_spacing} || -1,
        $args{role}         // (die "Missing mandatory attribute 'role'"), #/
        $args{polygon}->serialize;
    
    bless $o, 'Slic3r::ExtrusionLoop::Packed';
    return $o;
}

# no-op
sub unpack { $_[0] }

sub split_at_index {
    my $self = shift;
    
    return Slic3r::ExtrusionPath->new(
        polyline        => $self->polygon->split_at_index(@_),
        role            => $self->role,
        flow_spacing    => $self->flow_spacing,
    );
}

sub split_at {
    my $self = shift;
    
    return Slic3r::ExtrusionPath->new(
        polyline        => $self->polygon->split_at(@_),
        role            => $self->role,
        flow_spacing    => $self->flow_spacing,
    );
}

sub split_at_first_point {
    my $self = shift;
    return $self->split_at_index(0);
}

sub first_point {
    my $self = shift;
    return $self->polygon->[0];
}

package Slic3r::ExtrusionLoop::Packed;
sub unpack {
    my $self = shift;
    
    my ($flow_spacing, $role, $polygon_s)
        = unpack Slic3r::ExtrusionLoop::PACK_FMT, $$self;
    
    return Slic3r::ExtrusionLoop->new(
        flow_spacing    => ($flow_spacing == -1) ? undef : $flow_spacing,
        role            => $role,
        polygon         => Slic3r::Polygon->deserialize($polygon_s),
    );
}

1;
