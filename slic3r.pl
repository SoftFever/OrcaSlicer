#!/usr/bin/perl

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/lib";
}

use Getopt::Long;
use Slic3r;
use Time::HiRes qw(gettimeofday tv_interval);
use XXX;

use constant PI => 4 * atan2(1, 1);

my %opt;
GetOptions(
    'help'                  => sub { usage() },

    'debug'                 => \$Slic3r::debug,
    'o|output'              => \$opt{output},
    
    # printer options
    'nozzle-diameter=f'         => \$Slic3r::nozzle_diameter,
    'print-center=s'            => \$Slic3r::print_center,
    'use-relative-e-distances'  => \$Slic3r::use_relative_e_distances,
    'z-offset=f'                => \$Slic3r::z_offset,
    
    # filament options
    'filament-diameter=f'           => \$Slic3r::filament_diameter,
    'filament-packing-density=f'    => \$Slic3r::filament_packing_density,
    
    # speed options
    'print-feed-rate=i'             => \$Slic3r::print_feed_rate,
    'travel-feed-rate=i'            => \$Slic3r::travel_feed_rate,
    'perimeter-feed-rate=i'         => \$Slic3r::perimeter_feed_rate,
    'bottom-layer-speed-ratio=f'    => \$Slic3r::bottom_layer_speed_ratio,
    
    # accuracy options
    'layer-height=f'        => \$Slic3r::layer_height,
    
    # print options
    'perimeters=i'          => \$Slic3r::perimeter_offsets,
    'solid-layers=i'        => \$Slic3r::solid_layers,
    'fill-density=f'        => \$Slic3r::fill_density,
    'fill-angle=i'          => \$Slic3r::fill_angle,
    'temperature=i'         => \$Slic3r::temperature,
    
    # retraction options
    'retract-length=f'          => \$Slic3r::retract_length,
    'retract-speed=i'           => \$Slic3r::retract_speed,
    'retract-restart-extra=f'   => \$Slic3r::retract_restart_extra,
    'retract-before-travel=f'   => \$Slic3r::retract_before_travel,
    
    # skirt options
    'skirts=i'              => \$Slic3r::skirts,
    'skirt-distance=i'      => \$Slic3r::skirt_distance,
    
    # transform options
    'scale=i'               => \$Slic3r::scale,
    'rotate=i'              => \$Slic3r::rotate,
    'multiply-x=i'          => \$Slic3r::multiply_x,
    'multiply-y=i'          => \$Slic3r::multiply_y,
    'multiply-distance=i'   => \$Slic3r::multiply_distance,
);

# validate configuration
{
    # --layer-height
    die "Invalid value for --layer-height\n"
        if $Slic3r::layer_height < 0;
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

my $action = 'skein';

if ($action eq 'skein') {
    my $input_file = $ARGV[0] or usage(1);
    die "Input file must have .stl extension\n" 
        if $input_file !~ /\.stl$/i;
    
    my $t0 = [gettimeofday];
    my $print = Slic3r::Print->new_from_stl($input_file);
    $print->extrude_perimeters;
    $print->remove_small_features;
    $print->extrude_fills;
    
    my $output_file = $input_file;
    $output_file =~ s/\.stl$/.gcode/i;
    $print->export_gcode($opt{output} || $output_file);
    
    my $processing_time = tv_interval($t0);
    printf "Done. Process took %d minutes and %.3f seconds\n", 
        int($processing_time/60), $processing_time - int($processing_time/60)*60;
}

sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Slic3r is a STL-to-GCODE translator for RepRap 3D printers
written by Alessandro Ranellucci <aar\@cpan.org>

Usage: slic3r.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    
  Printer options:
    --nozzle-diameter   Diameter of nozzle in mm (default: $Slic3r::nozzle_diameter)
    --print-center      Coordinates of the point to center the print around 
                        (default: $Slic3r::print_center->[0],$Slic3r::print_center->[1])
    --use-relative-e-distances
                        Use relative distances for extrusion in GCODE output
    --z-offset          Additional height in mm to add to vertical coordinates
                        (+/-, default: $Slic3r::z_offset)
    
  Filament options:
    --filament-diameter Diameter of your raw filament (default: $Slic3r::filament_diameter)
    --filament-packing-density
                        Ratio of the extruded volume over volume pushed 
                        into the extruder (default: $Slic3r::filament_packing_density)
    
  Speed options:
    --print-feed-rate   Speed of print moves in mm/sec (default: $Slic3r::print_feed_rate)
    --travel-feed-rate  Speed of non-print moves in mm/sec (default: $Slic3r::travel_feed_rate)
    --perimeter-feed-rate
                        Speed of print moves for perimeters in mm/sec (default: $Slic3r::print_feed_rate)
    --bottom-layer-speed-ratio
                        Factor to increase/decrease speeds on bottom 
                        layer by (default: $Slic3r::bottom_layer_speed_ratio)
    
  Accuracy options:
    --layer-height      Layer height in mm (default: $Slic3r::layer_height)
  
  Print options:
    --perimeters        Number of perimeters/horizontal skins (range: 1+, 
                        default: $Slic3r::perimeter_offsets)
    --solid-layers      Number of solid layers to do for top/bottom surfaces
                        (range: 1+, default: $Slic3r::solid_layers)
    --fill-density      Infill density (range: 0-1, default: $Slic3r::fill_density)
    --fill-angle        Infill angle in degrees (range: 0-90, default: $Slic3r::fill_angle)
    --temperature       Extrusion temperature (default: $Slic3r::temperature)
  
  Retraction options:
    --retract-length    Length of retraction in mm when pausing extrusion 
                        (default: $Slic3r::retract_length)
    --retract-speed     Speed for retraction in mm/sec (default: $Slic3r::retract_speed)
    --retract-restart-extra
                        Additional amount of filament in mm to push after
                        compensating retraction (default: $Slic3r::retract_restart_extra)
    --retract-before-travel
                        Only retract before travel moves of this length (default: $Slic3r::retract_before_travel)
   
   Skirt options:
    --skirts            Number of skirts to draw (default: $Slic3r::skirts)
    --skirt-distance    Distance in mm between innermost skirt and object 
                        (default: $Slic3r::skirt_distance)
    -o, --output        File to output gcode to (default: <inputfile>.gcode)
   
   Transform options:
    --scale             Factor for scaling input object (default: $Slic3r::scale)
    --rotate            Rotation angle in degrees (0-360, default: $Slic3r::rotate)
    --multiply-x        Number of items along X axis (1+, default: $Slic3r::multiply_x)
    --multiply-y        Number of items along Y axis (1+, default: $Slic3r::multiply_y)
    --multiply-distance Distance in mm between copies (default: $Slic3r::multiply_distance)
    
EOF
    exit ($exit_code || 0);
}

__END__
