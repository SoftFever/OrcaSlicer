package Slic3r::Flow;
use Moo;

use Slic3r::Geometry qw(PI);

has 'nozzle_diameter'   => (is => 'rw', required => 1);
has 'layer_height'      => (is => 'rw', default => sub { $Slic3r::layer_height });

has 'width'             => (is => 'rw');
has 'min_spacing'       => (is => 'rw');
has 'spacing'           => (is => 'rw');

sub BUILD {
    my $self = shift;
    
    my ($flow_width, $min_flow_spacing, $flow_spacing);
    if ($self->width) {
        $flow_width = $self->width =~ /^(\d+(?:\.\d+)?)%$/
            ? ($self->layer_height * $1 / 100)
            : $self->width;
    } else {
        # here we calculate a sane default by matching the flow speed (at the nozzle)
        # and the feed rate
        my $volume = ($self->nozzle_diameter**2) * PI/4;
        my $shape_threshold = $self->nozzle_diameter * $self->layer_height
            + ($self->layer_height**2) * PI/4;
        if ($volume >= $shape_threshold) {
            # rectangle with semicircles at the ends
            $flow_width = (($self->nozzle_diameter**2) * PI + ($self->layer_height**2) * (4 - PI)) / (4 * $self->layer_height);
        } else {
            # rectangle with squished semicircles at the ends
            $flow_width = $self->nozzle_diameter * ($self->nozzle_diameter/$self->layer_height - 4/PI + 1);
        }
        
        my $min_flow_width = $self->nozzle_diameter * 1.05;
        my $max_flow_width = $self->nozzle_diameter * 1.4;
        $flow_width = $max_flow_width if $flow_width > $max_flow_width;
        $flow_width = $min_flow_width if $flow_width < $min_flow_width;
    }
    
    if ($flow_width >= ($self->nozzle_diameter + $self->layer_height)) {
        # rectangle with semicircles at the ends
        $min_flow_spacing = $flow_width - $self->layer_height * (1 - PI/4);
    } else {
        # rectangle with shrunk semicircles at the ends
        $min_flow_spacing = $flow_width * (1 - PI/4) + $self->nozzle_diameter * PI/4;
    }
    $flow_spacing = $flow_width - $Slic3r::overlap_factor * ($flow_width - $min_flow_spacing);
    
    $self->width($flow_width);
    $self->min_spacing($min_flow_spacing);
    $self->spacing($flow_spacing);
}

1;
