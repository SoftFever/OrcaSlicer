package Slic3r::Surface;
use Moo;

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

has 'surface_type' => (
    is      => 'rw',
    #isa     => enum([qw(internal internal-solid bottom top)]),
);

# this integer represents the thickness of the surface expressed in layers
has 'depth_layers' => (is => 'ro', default => sub {1});

sub cast_from_polygon {
    my $class = shift;
    my ($polygon, %args) = @_;
    
    return $class->new(
        contour      => Slic3r::Polyline::Closed->cast($polygon),
        %args,
    );
}

sub cast_from_expolygon {
    my $class = shift;
    my ($expolygon, %args) = @_;
    
    if (ref $expolygon eq 'HASH') {
        $expolygon = Slic3r::ExPolygon->new($expolygon);
    }
    
    return $class->new(
        contour => $expolygon->contour->closed_polyline,
        holes   => [ map $_->closed_polyline, $expolygon->holes ],
        %args,
    );
}

sub add_hole {
    my $self = shift;
    my ($hole) = @_;
    
    push @{ $self->holes }, $hole;
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
    return ($self->contour->p, map $_->p, @{$self->holes});
}

sub expolygon {
    my $self = shift;
    return Slic3r::ExPolygon->new($self->contour->p, map $_->p, @{$self->holes});
}

sub lines {
    my $self = shift;
    return @{ $self->contour->lines }, map @{ $_->lines }, @{ $self->holes };
}

1;
