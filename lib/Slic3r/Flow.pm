package Slic3r::Flow;
use Moo;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK   = qw(FLOW_ROLE_PERIMETER FLOW_ROLE_INFILL FLOW_ROLE_SOLID_INFILL FLOW_ROLE_TOP_SOLID_INFILL
                        FLOW_ROLE_SUPPORT_MATERIAL FLOW_ROLE_SUPPORT_MATERIAL_INTERFACE);
our %EXPORT_TAGS = (roles => \@EXPORT_OK);

use Slic3r::Geometry qw(PI);

has 'width'             => (is => 'ro');
has 'spacing'           => (is => 'ro');
has 'scaled_width'      => (is => 'lazy');
has 'scaled_spacing'    => (is => 'lazy');

use constant FLOW_ROLE_PERIMETER                    => 1;
use constant FLOW_ROLE_INFILL                       => 2;
use constant FLOW_ROLE_SOLID_INFILL                 => 3;
use constant FLOW_ROLE_TOP_SOLID_INFILL             => 4;
use constant FLOW_ROLE_SUPPORT_MATERIAL             => 5;
use constant FLOW_ROLE_SUPPORT_MATERIAL_INTERFACE   => 6;

sub BUILDARGS {
    my ($self, %args) = @_;
    
    # the constructor can take two sets of arguments:
    # - width (only absolute value), spacing
    # - width (abs/%/0), role, nozzle_diameter, layer_height, bridge_flow_ratio
    #   (if bridge_flow_ratio == 0, we return a non-bridge flow)
    
    if (exists $args{role}) {
        if ($args{width} eq '0') {
            $args{width} = $self->_width(@args{qw(role nozzle_diameter layer_height bridge_flow_ratio)});
        } elsif ($args{width} =~ /^(\d+(?:\.\d+)?)%$/) {
            $args{width} = $args{layer_height} * $1 / 100;
        }
        $args{spacing} = $self->_spacing(@args{qw(width nozzle_diameter layer_height bridge_flow_ratio)});
        %args = @args{qw(width spacing)};
    }
    
    return {%args};
}

sub _width {
    my ($self, $role, $nozzle_diameter, $layer_height, $bridge_flow_ratio) = @_;
    
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

sub _spacing {
    my ($self, $width, $nozzle_diameter, $layer_height, $bridge_flow_ratio) = @_;
    
    if ($bridge_flow_ratio > 0) {
        return $width + 0.05;
    }
    
    my $min_flow_spacing;
    if ($width >= ($nozzle_diameter + $layer_height)) {
        # rectangle with semicircles at the ends
        $min_flow_spacing = $width - $layer_height * (1 - PI/4);
    } else {
        # rectangle with shrunk semicircles at the ends
        $min_flow_spacing = $nozzle_diameter * (1 - PI/4) + $width * PI/4;
    }
    return $width - &Slic3r::OVERLAP_FACTOR * ($width - $min_flow_spacing);
}

sub clone {
    my $self = shift;
    
    return (ref $self)->new(
        width   => $self->width,
        spacing => $self->spacing,
    );
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
