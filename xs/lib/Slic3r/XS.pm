package Slic3r::XS;
use warnings;
use strict;

our $VERSION = '0.01';

use Carp qw();
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

package Slic3r::Point3;
use overload
    '@{}' => sub { [ $_[0]->x, $_[0]->y, $_[0]->z ] },  #,
    'fallback' => 1;

sub pp {
    my ($self) = @_;
    return [ @$self ];
}

package Slic3r::Pointf;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Pointf3;
use overload
    '@{}' => sub { [ $_[0]->x, $_[0]->y, $_[0]->z ] },  #,
    'fallback' => 1;

sub pp {
    my ($self) = @_;
    return [ @$self ];
}

package Slic3r::ExPolygon;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Polyline;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Polyline::Collection;
use overload
    '@{}' => sub { $_[0]->arrayref },
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

sub new_from_paths {
    my ($class, @paths) = @_;
    
    my $loop = $class->new;
    $loop->append($_) for @paths;
    return $loop;
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
        $args{mm3_per_mm}   // die("Missing required mm3_per_mm in ExtrusionPath constructor"),
        $args{width}        // -1,
        $args{height}       // -1,
    );
}

sub clone {
    my ($self, %args) = @_;
    
    return __PACKAGE__->_new(
        $args{polyline}      // $self->polyline,
        $args{role}          // $self->role,
        $args{mm3_per_mm}    // $self->mm3_per_mm,
        $args{width}         // $self->width,
        $args{height}        // $self->height,
    );
}

package Slic3r::Flow;

sub new {
    my ($class, %args) = @_;
    
    my $self = $class->_new(
        @args{qw(width height nozzle_diameter)},
    );
    $self->set_bridge($args{bridge} // 0);
    return $self;
}

sub new_from_width {
    my ($class, %args) = @_;
    
    return $class->_new_from_width(
        @args{qw(role width nozzle_diameter layer_height bridge_flow_ratio)},
    );
}

sub new_from_spacing {
    my ($class, %args) = @_;
    
    return $class->_new_from_spacing(
        @args{qw(spacing nozzle_diameter layer_height bridge)},
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
        delete $args{expolygon}         // $self->expolygon,
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

sub new {
    my ($class, @surfaces) = @_;
    
    my $self = $class->_new;
    $self->append($_) for @surfaces;
    return $self;
}

package Slic3r::GUI::_3DScene::GLVertexArray;
sub CLONE_SKIP { 1 }

package main;
for my $class (qw(
        Slic3r::BridgeDetector
        Slic3r::Config
        Slic3r::Config::Full
        Slic3r::Config::GCode
        Slic3r::Config::Print
        Slic3r::Config::PrintObject
        Slic3r::Config::PrintRegion
        Slic3r::ExPolygon
        Slic3r::ExPolygon::Collection
        Slic3r::Extruder
        Slic3r::ExtrusionLoop
        Slic3r::ExtrusionPath
        Slic3r::ExtrusionPath::Collection
        Slic3r::Flow
        Slic3r::GCode::PlaceholderParser
        Slic3r::Geometry::BoundingBox
        Slic3r::Geometry::BoundingBoxf
        Slic3r::Geometry::BoundingBoxf3
        Slic3r::Layer
        Slic3r::Layer::Region
        Slic3r::Layer::Support
        Slic3r::Line
        Slic3r::Linef3
        Slic3r::Model
        Slic3r::Model::Instance
        Slic3r::Model::Material
        Slic3r::Model::Object
        Slic3r::Model::Volume
        Slic3r::Point
        Slic3r::Point3
        Slic3r::Pointf
        Slic3r::Pointf3
        Slic3r::Polygon
        Slic3r::Polyline
        Slic3r::Polyline::Collection
        Slic3r::Print
        Slic3r::Print::Object
        Slic3r::Print::Region
        Slic3r::Print::State
        Slic3r::Surface
        Slic3r::Surface::Collection
        Slic3r::TriangleMesh
    ))
{
    no strict 'refs';
    my $ref_class = $class . "::Ref";
    eval "package $ref_class; our \@ISA = '$class'; sub DESTROY {};";
}

1;
