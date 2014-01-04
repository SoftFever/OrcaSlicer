package Slic3r::Flow;
use Moo;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK   = qw(FLOW_ROLE_PERIMETER FLOW_ROLE_INFILL FLOW_ROLE_SOLID_INFILL FLOW_ROLE_TOP_SOLID_INFILL
                        FLOW_ROLE_SUPPORT_MATERIAL FLOW_ROLE_SUPPORT_MATERIAL_INTERFACE);
our %EXPORT_TAGS = (roles => \@EXPORT_OK);

use Slic3r::Geometry qw(PI);

has 'width'             => (is => 'ro', required => 1);
has 'spacing'           => (is => 'ro', required => 1);
has 'nozzle_diameter'   => (is => 'ro', required => 1);
has 'bridge'            => (is => 'ro', default => sub {0});
has 'scaled_width'      => (is => 'lazy');
has 'scaled_spacing'    => (is => 'lazy');

use constant FLOW_ROLE_PERIMETER                    => 1;
use constant FLOW_ROLE_INFILL                       => 2;
use constant FLOW_ROLE_SOLID_INFILL                 => 3;
use constant FLOW_ROLE_TOP_SOLID_INFILL             => 4;
use constant FLOW_ROLE_SUPPORT_MATERIAL             => 5;
use constant FLOW_ROLE_SUPPORT_MATERIAL_INTERFACE   => 6;

use constant BRIDGE_EXTRA_SPACING   => 0.05;
use constant OVERLAP_FACTOR         => 1;

sub new_from_width {
    my ($class, %args) = @_;
    
    if ($args{width} eq '0') {
        $args{width} = _width(@args{qw(role nozzle_diameter layer_height bridge_flow_ratio)});
    } elsif ($args{width} =~ /^(\d+(?:\.\d+)?)%$/) {
        $args{width} = $args{layer_height} * $1 / 100;
    }
    
    return $class->new(
        width           => $args{width},
        spacing         => _spacing(@args{qw(width nozzle_diameter layer_height bridge_flow_ratio)}),
        nozzle_diameter => $args{nozzle_diameter},
        bridge          => ($args{bridge_flow_ratio} > 0) ? 1 : 0,
    );
}

sub new_from_spacing {
    my ($class, %args) = @_;
    
    return $class->new(
        width           => _width_from_spacing(@args{qw(spacing nozzle_diameter layer_height bridge)}),
        spacing         => $args{spacing},
        nozzle_diameter => $args{nozzle_diameter},
        bridge          => $args{bridge},
    );
}

sub clone {
    my $self = shift;
    
    return (ref $self)->new(
        width           => $self->width,
        spacing         => $self->spacing,
        nozzle_diameter => $self->nozzle_diameter,
        bridge          => $self->bridge,
    );
}

sub mm3_per_mm {
    my ($self, $h) = @_;
    
    my $w = $self->width;
    my $s = $self->spacing;
    
    if ($self->bridge) {
        return ($w**2) * PI/4;
    } elsif ($w >= ($self->nozzle_diameter + $h)) {
        # rectangle with semicircles at the ends
        return $w * $h + ($h**2) / 4 * (PI - 4);
    } else {
        # rectangle with shrunk semicircles at the ends
        return $self->nozzle_diameter * $h * (1 - PI/4) + $h * $w * PI/4;
    }
}

sub _width {
    my ($role, $nozzle_diameter, $layer_height, $bridge_flow_ratio) = @_;
    
    if ($bridge_flow_ratio > 0) {
        return sqrt($bridge_flow_ratio * ($nozzle_diameter**2));
    }
    
    # here we calculate a sane default by matching the flow speed (at the nozzle) and the feed rate
    my $volume = ($nozzle_diameter**2) * PI/4;
    my $shape_threshold = $nozzle_diameter * $layer_height + ($layer_height**2) * PI/4;
    my $width;
    if ($volume >= $shape_threshold) {
        # rectangle with semicircles at the ends
        $width = (($nozzle_diameter**2) * PI + ($layer_height**2) * (4 - PI)) / (4 * $layer_height);
    } else {
        # rectangle with squished semicircles at the ends
        $width = $nozzle_diameter * ($nozzle_diameter/$layer_height - 4/PI + 1);
    }
    
    my $min = $nozzle_diameter * 1.05;
    my $max;
    if ($role == FLOW_ROLE_PERIMETER || $role == FLOW_ROLE_SUPPORT_MATERIAL) {
        $min = $max = $nozzle_diameter;
    } elsif ($role != FLOW_ROLE_INFILL) {
        # do not limit width for sparse infill so that we use full native flow for it
        $max = $nozzle_diameter * 1.7;
    }
    $width = $max if defined($max) && $width > $max;
    $width = $min if $width < $min;
    
    return $width;
}

sub _width_from_spacing {
    my ($s, $nozzle_diameter, $h, $bridge) = @_;
    
    if ($bridge) {
        return $s - BRIDGE_EXTRA_SPACING;
    }
    
    my $w_threshold = $h + $nozzle_diameter;
    my $s_threshold = $w_threshold - OVERLAP_FACTOR * ($w_threshold - ($w_threshold - $h * (1 - PI/4)));
    
    if ($s >= $s_threshold) {
        # rectangle with semicircles at the ends
        return $s + OVERLAP_FACTOR * $h * (1 - PI/4);
    } else {
        # rectangle with shrunk semicircles at the ends
        return ($s + $nozzle_diameter * OVERLAP_FACTOR * (PI/4 - 1)) / (1 + OVERLAP_FACTOR * (PI/4 - 1));
    }
}

sub _spacing {
    my ($width, $nozzle_diameter, $layer_height, $bridge_flow_ratio) = @_;
    
    if ($bridge_flow_ratio > 0) {
        return $width + BRIDGE_EXTRA_SPACING;
    }
    use XXX; ZZZ "here" if !defined $nozzle_diameter;
    my $min_flow_spacing;
    if ($width >= ($nozzle_diameter + $layer_height)) {
        # rectangle with semicircles at the ends
        $min_flow_spacing = $width - $layer_height * (1 - PI/4);
    } else {
        # rectangle with shrunk semicircles at the ends
        $min_flow_spacing = $nozzle_diameter * (1 - PI/4) + $width * PI/4;
    }
    return $width - OVERLAP_FACTOR * ($width - $min_flow_spacing);
}

sub _build_scaled_width {
    my $self = shift;
    return Slic3r::Geometry::scale($self->width);
}

sub _build_scaled_spacing {
    my $self = shift;
    return Slic3r::Geometry::scale($self->spacing);
}

1;
