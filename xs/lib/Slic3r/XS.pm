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

package Slic3r::Line::Ref;
our @ISA = 'Slic3r::Line';

sub DESTROY {}

package Slic3r::Point;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Point::Ref;
our @ISA = 'Slic3r::Point';

sub DESTROY {}

package Slic3r::Pointf;
use overload
    '@{}' => sub { [ $_[0]->x, $_[0]->y ] },  #,
    'fallback' => 1;

package Slic3r::Pointf::Ref;
our @ISA = 'Slic3r::Pointf';

sub DESTROY {}

package Slic3r::Pointf3;
use overload
    '@{}' => sub { [ $_[0]->x, $_[0]->y, $_[0]->z ] },  #,
    'fallback' => 1;

package Slic3r::ExPolygon;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::ExPolygon::Ref;
our @ISA = 'Slic3r::ExPolygon';

sub DESTROY {}

package Slic3r::Polyline;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Polyline::Ref;
our @ISA = 'Slic3r::Polyline';

sub DESTROY {}

package Slic3r::Polyline::Collection;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Polygon;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Polygon::Ref;
our @ISA = 'Slic3r::Polygon';

sub DESTROY {}

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

package Slic3r::ExtrusionPath::Collection::Ref;
our @ISA = 'Slic3r::ExtrusionPath::Collection';

sub DESTROY {}

package Slic3r::ExtrusionLoop;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

sub new {
    my ($class, %args) = @_;
    
    return $class->_new(
        $args{polygon},      # required
        $args{role},         # required
        $args{mm3_per_mm}   // die("Missing required mm3_per_mm in ExtrusionLoop constructor"),
        $args{width}        // -1,
        $args{height}       // -1,
    );
}

sub clone {
    my ($self, %args) = @_;
    
    return __PACKAGE__->_new(
        $args{polygon}       // $self->polygon,
        $args{role}          // $self->role,
        $args{mm3_per_mm}    // $self->mm3_per_mm,
        $args{width}         // $self->width,
        $args{height}        // $self->height,
    );
}

package Slic3r::ExtrusionLoop::Ref;
our @ISA = 'Slic3r::ExtrusionLoop';

sub DESTROY {}

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

package Slic3r::ExtrusionPath::Ref;
our @ISA = 'Slic3r::ExtrusionPath';

sub DESTROY {}

package Slic3r::Flow;

sub new {
    my ($class, %args) = @_;
    
    my $self = $class->_new(
        @args{qw(width spacing nozzle_diameter)},
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

package Slic3r::Surface::Ref;
our @ISA = 'Slic3r::Surface';

sub DESTROY {}

package Slic3r::Surface::Collection;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

1;
