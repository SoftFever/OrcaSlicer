package Slic3r::Flow;
use Moo;

use Slic3r::Geometry qw(PI);

has 'nozzle_diameter'   => (is => 'ro', required => 1);
has 'layer_height'      => (is => 'ro', default => sub { $Slic3r::Config->layer_height });

has 'width'             => (is => 'rwp', builder => 1);
has 'spacing'           => (is => 'lazy');

sub BUILD {
    my $self = shift;
    
    if ($self->width =~ /^(\d+(?:\.\d+)?)%$/) {
        $self->_set_width($self->layer_height * $1 / 100);
    }
    $self->_set_width($self->_build_width) if $self->width == 0; # auto
}

sub _build_width {
    my $self = shift;
    
    # here we calculate a sane default by matching the flow speed (at the nozzle) and the feed rate
    my $volume = ($self->nozzle_diameter**2) * PI/4;
    my $shape_threshold = $self->nozzle_diameter * $self->layer_height + ($self->layer_height**2) * PI/4;
    my $width;
    if ($volume >= $shape_threshold) {
        # rectangle with semicircles at the ends
        $width = (($self->nozzle_diameter**2) * PI + ($self->layer_height**2) * (4 - PI)) / (4 * $self->layer_height);
    } else {
        # rectangle with squished semicircles at the ends
        $width = $self->nozzle_diameter * ($self->nozzle_diameter/$self->layer_height - 4/PI + 1);
    }
    
    my $min = $self->nozzle_diameter * 1.05;
    my $max = $self->nozzle_diameter * 1.4;
    $width = $max if $width > $max;
    $width = $min if $width < $min;
    
    return $width;
}

sub _build_spacing {
    my $self = shift;
    
    my $min_flow_spacing;
    if ($self->width >= ($self->nozzle_diameter + $self->layer_height)) {
        # rectangle with semicircles at the ends
        $min_flow_spacing = $self->width - $self->layer_height * (1 - PI/4);
    } else {
        # rectangle with shrunk semicircles at the ends
        $min_flow_spacing = $self->nozzle_diameter * (1 - PI/4) + $self->width * PI/4;
    }
    return $self->width - &Slic3r::OVERLAP_FACTOR * ($self->width - $min_flow_spacing);
}

1;
