package Slic3r::Config;
use strict;
use warnings;

use constant PI => 4 * atan2(1, 1);

sub validate {
    my $class = shift;

    # --layer-height
    die "Invalid value for --layer-height\n"
        if $Slic3r::layer_height <= 0;
    die "--layer-height must be a multiple of print resolution\n"
        if $Slic3r::layer_height / $Slic3r::resolution % 1 != 0;
    
    # --filament-diameter
    die "Invalid value for --filament-diameter\n"
        if $Slic3r::filament_diameter < 1;
    
    # --nozzle-diameter
    die "Invalid value for --nozzle-diameter\n"
        if $Slic3r::nozzle_diameter < 0;
    die "--layer-height can't be greater than --nozzle-diameter\n"
        if $Slic3r::layer_height > $Slic3r::nozzle_diameter;
    $Slic3r::flow_width = ($Slic3r::nozzle_diameter**2) 
        * $Slic3r::thickness_ratio * PI / (4 * $Slic3r::layer_height);
    
    my $max_flow_width = $Slic3r::layer_height + $Slic3r::nozzle_diameter;
    if ($Slic3r::flow_width > $max_flow_width) {
        $Slic3r::thickness_ratio = $max_flow_width / $Slic3r::flow_width;
        $Slic3r::flow_width = $max_flow_width;
    }
    
    Slic3r::debugf "Flow width = $Slic3r::flow_width\n";
    
    # --perimeters
    die "Invalid value for --perimeters\n"
        if $Slic3r::perimeter_offsets < 1;
    
    # --solid-layers
    die "Invalid value for --solid-layers\n"
        if $Slic3r::solid_layers < 1;
    
    # --print-center
    die "Invalid value for --print-center\n"
        if !ref $Slic3r::print_center 
            && (!$Slic3r::print_center || $Slic3r::print_center !~ /^\d+,\d+$/);
    $Slic3r::print_center = [ split /,/, $Slic3r::print_center ]
        if !ref $Slic3r::print_center;
    
    # --fill-density
    die "Invalid value for --fill-density\n"
        if $Slic3r::fill_density < 0 || $Slic3r::fill_density > 1;
    
    # --scale
    die "Invalid value for --scale\n"
        if $Slic3r::scale <= 0;
    
    # --multiply-x
    die "Invalid value for --multiply-x\n"
        if $Slic3r::multiply_x < 1;
    
    # --multiply-y
    die "Invalid value for --multiply-y\n"
        if $Slic3r::multiply_y < 1;
    
    # --multiply-distance
    die "Invalid value for --multiply-distance\n"
        if $Slic3r::multiply_distance < 1;
}

1;
