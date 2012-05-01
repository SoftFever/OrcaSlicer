package Slic3r::Surface;
use strict;
use warnings;

use constant S_EXPOLYGON    => 0;
use constant S_SURFACE_TYPE => 1;
use constant S_DEPTH_LAYERS => 2;
use constant S_BRIDGE_ANGLE => 3;

sub new {
    my $class = shift;
    my %args = @_;
    
    my $self = [
        map delete $args{$_}, qw(expolygon surface_type depth_layers bridge_angle),
    ];
    $self->[S_DEPTH_LAYERS] //= 1; #/
    
    bless $self, $class;
    $self;
}

sub expolygon       { $_[0][S_EXPOLYGON] }
sub surface_type    { $_[0][S_SURFACE_TYPE] = $_[1] if $_[1]; $_[0][S_SURFACE_TYPE] }
sub depth_layers    { $_[0][S_DEPTH_LAYERS] } # this integer represents the thickness of the surface expressed in layers
sub bridge_angle    { $_[0][S_BRIDGE_ANGLE] }

# delegate handles
sub encloses_point  { $_[0]->expolygon->encloses_point }
sub lines           { $_[0]->expolygon->lines }
sub contour         { $_[0]->expolygon->contour }
sub holes           { $_[0]->expolygon->holes }

# static method to group surfaces having same surface_type, bridge_angle and depth_layers
sub group {
    my $class = shift;
    my $params = ref $_[0] eq 'HASH' ? shift(@_) : {};
    my (@surfaces) = @_;
    
    my %unique_types = ();
    foreach my $surface (@surfaces) {
        my $type = ($params->{merge_solid} && $surface->surface_type =~ /top|bottom|solid/)
            ? 'solid'
            : $surface->surface_type;
        $type .= "_" . ($surface->bridge_angle // ''); #/
        $type .= "_" . $surface->depth_layers;
        $unique_types{$type} ||= [];
        push @{ $unique_types{$type} }, $surface;
    }
    
    return values %unique_types;
}

sub offset {
    my $self = shift;
    return map {
        (ref $self)->new(
            expolygon => $_,
            map { $_ => $self->$_ } qw(surface_type depth_layers bridge_angle),
        )
    } $self->expolygon->offset_ex(@_);
}

sub add_hole {
    my $self = shift;
    my ($hole) = @_;
    
    push @$self, $hole;
}

sub id {
    my $self = shift;
    return $self->contour->id;
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
    return @{$self->expolygon};
}

1;
