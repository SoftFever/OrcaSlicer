package Slic3r::Geometry::BoundingBox;
use Moo;

use List::Util qw(min max);
use Slic3r::Geometry qw(X Y Z MIN MAX X1 Y1 X2 Y2 Z1 Z2);
use Storable qw();

has 'extents' => (is => 'ro', required => 1);

sub clone { Storable::dclone($_[0]) }

# 2D
sub new_from_points {
    my $class = shift;
    my ($points) = @_;
    
    my $bb = [ Slic3r::Geometry::bounding_box($points) ];
    return $class->new(extents => [
        [ $bb->[X1], $bb->[X2] ],
        [ $bb->[Y1], $bb->[Y2] ],
    ]);
}

# 2D/3D
sub new_from_bb {
    my $class = shift;
    my ($bb) = @_;
    
    return $class->new(extents => [
        [ $bb->[X1], $bb->[X2] ],
        [ $bb->[Y1], $bb->[Y2] ],
        (@$bb == 6) ? [ $bb->[Z1], $bb->[Z2] ] : (),
    ]);
}

# 3D
sub new_from_points_3D {
    my $class = shift;
    my ($points) = @_;
    
    return $class->new(extents => [ Slic3r::Geometry::bounding_box_3D($points) ]);
}

sub merge {
    my $class = shift;
    my (@bounding_boxes) = @_;
    
    my $self = ref($class)
        ? $class
        : shift @bounding_boxes;
    
    foreach my $bounding_box (@bounding_boxes) {
        for my $axis (X .. $#{$self->extents}) {
            $self->extents->[$axis][MIN] = min($self->extents->[$axis][MIN], $bounding_box->extents->[$axis][MIN]);
            $self->extents->[$axis][MAX] = max($self->extents->[$axis][MAX], $bounding_box->extents->[$axis][MAX]);
        }
    }
    
    return $self;
}

# four-arguments 2D bb
sub bb {
    my $self = shift;
    my $extents = $self->extents;
    return [ $extents->[X][MIN], $extents->[Y][MIN], $extents->[X][MAX], $extents->[Y][MAX] ];
}

sub polygon {
    my $self = shift;
    
    my $e = $self->extents;
    return Slic3r::Polygon->new(
        [ $e->[X][MIN], $e->[Y][MIN] ],
        [ $e->[X][MAX], $e->[Y][MIN] ],
        [ $e->[X][MAX], $e->[Y][MAX] ],
        [ $e->[X][MIN], $e->[Y][MAX] ],
    );
}

# note to $self
sub rotate {
    die "Rotating an axis-aligned bounding box doesn't make any sense";
}

sub scale {
    my $self = shift;
    my ($factor) = @_;
    
    for (@{$self->extents}) {
        $_ *= $factor for @$_[MIN,MAX];
    }
    
    $self;
}

sub translate {
    my $self = shift;
    my @shift = @_;
    
    for my $axis (X .. $#{$self->extents}) {
        $self->extents->[$axis][MIN] += $shift[$axis];
        $self->extents->[$axis][MAX] += $shift[$axis];
    }
    
    $self;
}

sub size {
    my $self = shift;
    
    my $extents = $self->extents;
    return [ map $extents->[$_][MAX] - $extents->[$_][MIN], grep $extents->[$_], (X,Y,Z) ];
}

sub center {
    my $self = shift;
    
    my $extents = $self->extents;
    return [ map +($extents->[$_][MAX] + $extents->[$_][MIN])/2, grep $extents->[$_], (X,Y,Z) ];
}

sub center_2D {
    my $self = shift;
    return Slic3r::Point->new(@{$self->center}[X,Y]);
}

sub min_point {
    my $self = shift;
    return Slic3r::Point->new($self->extents->[X][MIN], $self->extents->[Y][MIN]);
}

sub max_point {
    my $self = shift;
    return Slic3r::Point->new($self->extents->[X][MAX], $self->extents->[Y][MAX]);
}

sub x_min {
    my $self = shift;
    return $self->extents->[X][MIN];
}

sub x_max {
    my $self = shift;
    return $self->extents->[X][MAX];
}

sub y_min {
    my $self = shift;
    return $self->extents->[Y][MIN];
}

sub y_max {
    my $self = shift;
    return $self->extents->[Y][MAX];
}

1;
