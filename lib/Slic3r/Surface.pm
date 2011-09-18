package Slic3r::Surface;
use Moo;

use Math::Geometry::Planar;

has 'contour' => (
    is          => 'ro',
    #isa         => 'Slic3r::Polyline::Closed',
    required    => 1,
);

has 'holes' => (
    traits  => ['Array'],
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::Polyline::Closed]',
    default => sub { [] },
);

# TODO: to allow for multiple solid skins to be filled near external
# surfaces, a new type should be defined: internal-solid
has 'surface_type' => (
    is      => 'rw',
    #isa     => enum([qw(internal bottom top)]),
);

sub add_hole {
    my $self = shift;
    my ($hole) = @_;
    
    push @{ $self->holes }, $hole;
}

sub new_from_mgp {
    my $self = shift;
    my ($polygon, %params) = @_;
    
    my ($contour_p, @holes_p) = @{ $polygon->polygons };
    
    return __PACKAGE__->new(
        contour => Slic3r::Polyline::Closed->cast($contour_p),
        holes   => [
            map Slic3r::Polyline::Closed->cast($_), @holes_p
        ],
        %params,
    );
}

sub id {
    my $self = shift;
    return $self->contour->id;
}

sub encloses_point {
    my $self = shift;
    my ($point) = @_;
    
    return 0 if !$self->contour->encloses_point($point);
    return 0 if grep $_->encloses_point($point), @{ $self->holes };
    return 1;
}

sub mgp_polygon {
    my $self = shift;
    
    my $p = Math::Geometry::Planar->new;
    $p->polygons([ $self->contour->p, map($_->p, @{ $self->holes }) ]);
    return $p;
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

sub lines {
    my $self = shift;
    return @{ $self->contour->lines }, map @{ $_->lines }, @{ $self->holes };
}

1;
