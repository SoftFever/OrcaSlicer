package Slic3r::Flow;
use Moo;

use Slic3r::Geometry qw(PI);

has 'width' => (is => 'rw');
has 'min_spacing' => (is => 'rw');
has 'spacing' => (is => 'rw');

sub calculate {
    my $self = shift;
    my ($extrusion_width) = @_;
    
    my ($flow_width, $min_flow_spacing, $flow_spacing);
    if ($extrusion_width) {
        $flow_width = $extrusion_width =~ /^(\d+(?:\.\d+)?)%$/
            ? ($Slic3r::layer_height * $1 / 100)
            : $extrusion_width;
    } else {
        # here we calculate a sane default by matching the flow speed (at the nozzle)
        # and the feed rate
        my $volume = ($Slic3r::nozzle_diameter**2) * PI/4;
        my $shape_threshold = $Slic3r::nozzle_diameter * $Slic3r::layer_height
            + ($Slic3r::layer_height**2) * PI/4;
        if ($volume >= $shape_threshold) {
            # rectangle with semicircles at the ends
            $flow_width = (($Slic3r::nozzle_diameter**2) * PI + ($Slic3r::layer_height**2) * (4 - PI)) / (4 * $Slic3r::layer_height);
        } else {
            # rectangle with squished semicircles at the ends
            $flow_width = $Slic3r::nozzle_diameter * ($Slic3r::nozzle_diameter/$Slic3r::layer_height - 4/PI + 1);
        }
        
        my $min_flow_width = $Slic3r::nozzle_diameter * 1.05;
        my $max_flow_width = $Slic3r::nozzle_diameter * 1.4;
        $flow_width = $max_flow_width if $flow_width > $max_flow_width;
        $flow_width = $min_flow_width if $flow_width < $min_flow_width;
    }
    
    if ($flow_width >= ($Slic3r::nozzle_diameter + $Slic3r::layer_height)) {
        # rectangle with semicircles at the ends
        $min_flow_spacing = $flow_width - $Slic3r::layer_height * (1 - PI/4);
    } else {
        # rectangle with shrunk semicircles at the ends
        $min_flow_spacing = $flow_width * (1 - PI/4) + $Slic3r::nozzle_diameter * PI/4;
    }
    $flow_spacing = $flow_width - $Slic3r::overlap_factor * ($flow_width - $min_flow_spacing);
    
    $self->width($flow_width);
    $self->min_spacing($min_flow_spacing);
    $self->spacing($flow_spacing);
}

1;
