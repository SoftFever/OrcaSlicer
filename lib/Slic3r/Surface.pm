package Slic3r::Surface;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK   = qw(S_TYPE_TOP S_TYPE_BOTTOM S_TYPE_INTERNAL S_TYPE_INTERNALSOLID S_TYPE_INTERNALBRIDGE S_TYPE_INTERNALVOID);
our %EXPORT_TAGS = (types => \@EXPORT_OK);

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
    return [ map $self->clone(expolygon => $_), @{$self->expolygon->offset_ex(@_)} ];
}

sub simplify {
    my $self = shift;
    return map $self->clone(expolygon => $_), $self->expolygon->simplify(@_);
}

sub p {
    my $self = shift;
    return @{$self->polygons};
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
