package Slic3r::XS;
use warnings;
use strict;

our $VERSION = '0.01';

use XSLoader;
XSLoader::load(__PACKAGE__, $VERSION);

package Slic3r::Line;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Point;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::ExPolygon;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Polyline;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1,
    'fallback' => 1;

package Slic3r::Polygon;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::ExPolygon::Collection;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::ExtrusionPath::Collection;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

sub new {
    my ($class, @paths) = @_;
    
    my $self = $class->_new;
    $self->append(@paths);
    return $self;
}

package Slic3r::ExtrusionLoop;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

sub new {
    my ($class, %args) = @_;
    
    return $class->_new(
        $args{polygon},      # required
        $args{role},         # required
        $args{height}        // -1,
        $args{flow_spacing}  // -1,
    );
}

sub clone {
    my ($self, %args) = @_;
    
    return (ref $self)->_new(
        $args{polygon}       // $self->polygon,
        $args{role}          // $self->role,
        $args{height}        // $self->height,
        $args{flow_spacing}  // $self->flow_spacing,
    );
}

package Slic3r::ExtrusionPath;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

sub new {
    my ($class, %args) = @_;
    
    return $class->_new(
        $args{polyline},     # required
        $args{role},         # required
        $args{height}        // -1,
        $args{flow_spacing}  // -1,
    );
}

sub clone {
    my ($self, %args) = @_;
    
    return (ref $self)->_new(
        $args{polyline}      // $self->polyline,
        $args{role}          // $self->role,
        $args{height}        // $self->height,
        $args{flow_spacing}  // $self->flow_spacing,
    );
}

package Slic3r::Surface;

sub new {
    my ($class, %args) = @_;
    
    # defensive programming: make sure no negative bridge_angle is supplied
    die "Error: invalid negative bridge_angle\n"
        if defined $args{bridge_angle} && $args{bridge_angle} < 0;
    
    return $class->_new(
        $args{expolygon}         // (die "Missing required expolygon\n"),
        $args{surface_type}      // (die "Missing required surface_type\n"),
        $args{thickness}         // -1,
        $args{thickness_layers}  // 1,
        $args{bridge_angle}      // -1,
        $args{extra_perimeters}  // 0,
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

package Slic3r::Surface::Collection;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

1;
