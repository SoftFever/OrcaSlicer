package Slic3r::Surface;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK   = qw(S_TYPE_TOP S_TYPE_BOTTOM S_TYPE_INTERNAL S_TYPE_INTERNALSOLID S_TYPE_INTERNALBRIDGE S_TYPE_INTERNALVOID);
our %EXPORT_TAGS = (types => \@EXPORT_OK);

use constant S_EXPOLYGON            => 0;
use constant S_SURFACE_TYPE         => 1;
use constant S_THICKNESS            => 2;  # in mm
use constant S_THICKNESS_LAYERS     => 3;  # in layers
use constant S_BRIDGE_ANGLE         => 4;
use constant S_EXTRA_PERIMETERS     => 5;

use constant S_TYPE_TOP             => 0;
use constant S_TYPE_BOTTOM          => 1;
use constant S_TYPE_INTERNAL        => 2;
use constant S_TYPE_INTERNALSOLID   => 3;
use constant S_TYPE_INTERNALBRIDGE  => 4;
use constant S_TYPE_INTERNALVOID    => 5;

sub new {
    my $class = shift;
    my %args = @_;
    
    my $self = [
        map delete $args{$_}, qw(expolygon surface_type thickness thickness_layers bridge_angle extra_perimeters),
    ];
    $self->[S_THICKNESS_LAYERS] //= 1;
    
    bless $self, $class;
    $self;
}

sub clone {
    my $self = shift;
    my %p = @_;
    
    return (ref $self)->new(
        (map { $_ => $self->$_ } qw(surface_type thickness thickness_layers bridge_angle)),
        expolygon => ($p{expolygon} ? delete $p{expolygon} : $self->expolygon->clone),
        %p,
    );
}

sub expolygon       { $_[0][S_EXPOLYGON] }
sub surface_type    { $_[0][S_SURFACE_TYPE] = $_[1] if defined $_[1]; $_[0][S_SURFACE_TYPE] }
sub thickness       { $_[0][S_THICKNESS] }
sub thickness_layers    { $_[0][S_THICKNESS_LAYERS] }
sub bridge_angle    { $_[0][S_BRIDGE_ANGLE] = $_[1] if defined $_[1]; $_[0][S_BRIDGE_ANGLE] }
sub extra_perimeters { $_[0][S_EXTRA_PERIMETERS] = $_[1] if defined $_[1]; $_[0][S_EXTRA_PERIMETERS] }

if (eval "use Class::XSAccessor::Array; 1") {
    Class::XSAccessor::Array->import(
        getters => {
            expolygon           => S_EXPOLYGON,
        },
        accessors => {
            surface_type        => S_SURFACE_TYPE,
            thickness           => S_THICKNESS,
            thickness_layers    => S_THICKNESS_LAYERS,
            bridge_angle        => S_BRIDGE_ANGLE,
            extra_perimeters    => S_EXTRA_PERIMETERS,
        },
        replace => 1,
    );
}

# delegate handles
sub encloses_point  { $_[0]->expolygon->encloses_point }
sub lines           { $_[0]->expolygon->lines }
sub contour         { $_[0]->expolygon->contour }
sub holes           { $_[0]->expolygon->holes }

# static method to group surfaces having same surface_type, bridge_angle and thickness*
sub group {
    my $class = shift;
    my $params = ref $_[0] eq 'HASH' ? shift(@_) : {};
    my (@surfaces) = @_;
    
    my %unique_types = ();
    foreach my $surface (@surfaces) {
        my $type = join '_',
            ($params->{merge_solid} && $surface->is_solid) ? 'solid' : $surface->surface_type,
            $surface->bridge_angle // '',
            $surface->thickness // '',
            $surface->thickness_layers;
        $unique_types{$type} ||= [];
        push @{ $unique_types{$type} }, $surface;
    }
    
    return values %unique_types;
}

sub offset {
    my $self = shift;
    return map $self->clone(expolygon => $_), $self->expolygon->offset_ex(@_);
}

sub simplify {
    my $self = shift;
    return map $self->clone(expolygon => $_), $self->expolygon->simplify(@_);
}

sub p {
    my $self = shift;
    return @{$self->expolygon};
}

sub is_solid {
    my $self = shift;
    my $type = $self->surface_type;
    # S_TYPE_INTERNALBRIDGE is not solid because we can't merge it with other solid types
    return $type == S_TYPE_TOP
        || $type == S_TYPE_BOTTOM
        || $type == S_TYPE_INTERNALSOLID;
}

sub is_bridge {
    my $self = shift;
    my $type = $self->surface_type;
    return $type == S_TYPE_BOTTOM
        || $type == S_TYPE_INTERNALBRIDGE;
}

1;
