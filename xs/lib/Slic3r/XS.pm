package Slic3r::XS;
use warnings;
use strict;

our $VERSION = '0.01';

use XSLoader;
XSLoader::load(__PACKAGE__, $VERSION);

package Slic3r::Point::XS;
use overload
    '@{}' => sub { $_[0]->arrayref };

package Slic3r::ExPolygon::XS;
use overload
    '@{}' => sub { $_[0]->arrayref };

sub clone { (ref $_[0])->_clone($_[0]) }

# to handle legacy code
sub rotate {
    my $self = shift;
    my ($angle, $center) = @_;
    
    $center = Slic3r::Point::XS->new(@$center) if ref($center) ne 'Slic3r::Point::XS';
    $self->_rotate($angle, $center);
}

package Slic3r::ExPolygon::Collection;
use overload
    '@{}' => sub { $_[0]->arrayref };

sub clone { (ref $_[0])->_clone($_[0]) }

package Slic3r::Surface;

sub new {
    my ($class, %args) = @_;
    
    # defensive programming: make sure no negative bridge_angle is supplied
    die "Error: invalid negative bridge_angle\n"
        if defined $args{bridge_angle} && $args{bridge_angle} < 0;
    
    return $class->_new(
        delete $args{expolygon},                # required
        delete $args{surface_type},             # required
        delete $args{thickness}         // -1,
        delete $args{thickness_layers}  // 1,
        delete $args{bridge_angle}      // -1,
        delete $args{extra_perimeters}  // 0,
    );
}

sub clone {
    my ($self, %args) = @_;
    
    return (ref $self)->_new(
        delete $args{expolygon}         // $self->expolygon->clone,
        delete $args{surface_type}      // $self->surface_type,
        delete $args{thickness}         // $self->thickness,
        delete $args{thickness_layers}  // $self->thickness_layers,
        delete $args{bridge_angle}      // $self->bridge_angle,
        delete $args{extra_perimeters}  // $self->extra_perimeters,
    );
}

1;
