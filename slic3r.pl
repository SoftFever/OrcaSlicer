#!/usr/bin/perl

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/lib";
}

use Getopt::Long;
use Slic3r;
use XXX;

my %opt;
GetOptions(
    'help'                  => sub { usage() },

    'debug'                 => \$Slic3r::debug,
    'o|output'              => \$opt{output},
    
    'save=s'                    => \$opt{save},
    'load=s'                    => \$opt{load},
    
    # printer options
    'nozzle-diameter=f'         => \$Slic3r::nozzle_diameter,
    'print-center=s'            => \$Slic3r::print_center,
    'use-relative-e-distances'  => \$Slic3r::use_relative_e_distances,
    'z-offset=f'                => \$Slic3r::z_offset,
    'gcode-arcs'                => \$Slic3r::gcode_arcs,
    
    # filament options
    'filament-diameter=f'           => \$Slic3r::filament_diameter,
    'filament-packing-density=f'    => \$Slic3r::filament_packing_density,
    'temperature=i'         => \$Slic3r::temperature,
    
    # speed options
    'print-feed-rate=i'             => \$Slic3r::print_feed_rate,
    'travel-feed-rate=i'            => \$Slic3r::travel_feed_rate,
    'perimeter-feed-rate=i'         => \$Slic3r::perimeter_feed_rate,
    'bottom-layer-speed-ratio=f'    => \$Slic3r::bottom_layer_speed_ratio,
    
    # accuracy options
    'layer-height=f'        => \$Slic3r::layer_height,
    'infill-every-layers=i' => \$Slic3r::infill_every_layers,
    
    # print options
    'perimeters=i'          => \$Slic3r::perimeter_offsets,
    'solid-layers=i'        => \$Slic3r::solid_layers,
    'fill-type=s'           => \$Slic3r::fill_type,
    'fill-density=f'        => \$Slic3r::fill_density,
    'fill-angle=i'          => \$Slic3r::fill_angle,
    'start-gcode=s'         => \$opt{start_gcode},
    'end-gcode=s'           => \$opt{end_gcode},
    
    # retraction options
    'retract-length=f'          => \$Slic3r::retract_length,
    'retract-speed=i'           => \$Slic3r::retract_speed,
    'retract-restart-extra=f'   => \$Slic3r::retract_restart_extra,
    'retract-before-travel=f'   => \$Slic3r::retract_before_travel,
    'retract-lift=f'            => \$Slic3r::retract_lift,
    
    # skirt options
    'skirts=i'              => \$Slic3r::skirts,
    'skirt-distance=i'      => \$Slic3r::skirt_distance,
    
    # transform options
    'scale=f'               => \$Slic3r::scale,
    'rotate=i'              => \$Slic3r::rotate,
    'duplicate-x=i'         => \$Slic3r::duplicate_x,
    'duplicate-y=i'         => \$Slic3r::duplicate_y,
    'duplicate-distance=i'  => \$Slic3r::duplicate_distance,
);

# load configuration
if ($opt{load}) {
    -e $opt{load} or die "Cannot find specified configuration file.\n";
    Slic3r::Config->load($opt{load});
}

# validate command line options
Slic3r::Config->validate_cli(\%opt);

# validate configuration
Slic3r::Config->validate;

# save configuration
Slic3r::Config->save($opt{save}) if $opt{save};

# start GUI
if (!@ARGV && !$opt{save} && eval "require Slic3r::GUI; 1") {
    Slic3r::GUI->new->MainLoop;
    exit;
}

if ($ARGV[0]) {

    # skein
    my $input_file = $ARGV[0];
    
    my $skein = Slic3r::Skein->new(
        input_file  => $input_file,
        output_file => $opt{output},
    );
    $skein->go;
    
} else {
    usage(1) unless $opt{save};
}

sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Slic3r is a STL-to-GCODE translator for RepRap 3D printers
written by Alessandro Ranellucci <aar\@cpan.org> - http://slic3r.org/

Usage: slic3r.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    --save <file>       Save configuration to the specified file
    --load <file>       Load configuration from the specified file
    -o, --output        File to output gcode to (default: <inputfile>.gcode)
    
  Printer options:
    --nozzle-diameter   Diameter of nozzle in mm (default: $Slic3r::nozzle_diameter)
    --print-center      Coordinates of the point to center the print around 
                        (default: $Slic3r::print_center->[0],$Slic3r::print_center->[1])
    --use-relative-e-distances
                        Use relative distances for extrusion in GCODE output
    --z-offset          Additional height in mm to add to vertical coordinates
                        (+/-, default: $Slic3r::z_offset)
    --gcode-arcs        Use G2/G3 commands for native arcs (experimental, not supported
                        by all firmwares)
    
  Filament options:
    --filament-diameter Diameter in mm of your raw filament (default: $Slic3r::filament_diameter)
    --filament-packing-density
                        Ratio of the extruded volume over volume pushed 
                        into the extruder (default: $Slic3r::filament_packing_density)
    --temperature       Extrusion temperature (default: $Slic3r::temperature)
    
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
    --infill-every-layers
                        Infill every N layers (default: $Slic3r::infill_every_layers)
  
  Print options:
    --perimeters        Number of perimeters/horizontal skins (range: 1+, 
                        default: $Slic3r::perimeter_offsets)
    --solid-layers      Number of solid layers to do for top/bottom surfaces
                        (range: 1+, default: $Slic3r::solid_layers)
    --fill-density      Infill density (range: 0-1, default: $Slic3r::fill_density)
    --fill-angle        Infill angle in degrees (range: 0-90, default: $Slic3r::fill_angle)
    --start-gcode       Load initial gcode from the supplied file. This will overwrite
                        the default command (home all axes [G28]).
    --end-gcode         Load final gcode from the supplied file. This will overwrite 
                        the default commands (turn off temperature [M104 S0],
                        home X axis [G28 X], disable motors [M84]).
  
  Retraction options:
    --retract-length    Length of retraction in mm when pausing extrusion 
                        (default: $Slic3r::retract_length)
    --retract-speed     Speed for retraction in mm/sec (default: $Slic3r::retract_speed)
    --retract-restart-extra
                        Additional amount of filament in mm to push after
                        compensating retraction (default: $Slic3r::retract_restart_extra)
    --retract-before-travel
                        Only retract before travel moves of this length (default: $Slic3r::retract_before_travel)
    --retract-lift      Lift Z by the given distance in mm when retracting (default: $Slic3r::retract_lift)
   
   Skirt options:
    --skirts            Number of skirts to draw (default: $Slic3r::skirts)
    --skirt-distance    Distance in mm between innermost skirt and object 
                        (default: $Slic3r::skirt_distance)
   
   Transform options:
    --scale             Factor for scaling input object (default: $Slic3r::scale)
    --rotate            Rotation angle in degrees (0-360, default: $Slic3r::rotate)
    --duplicate-x       Number of items along X axis (1+, default: $Slic3r::duplicate_x)
    --duplicate-y       Number of items along Y axis (1+, default: $Slic3r::duplicate_y)
    --duplicate-distance Distance in mm between copies (default: $Slic3r::duplicate_distance)
    
EOF
    exit ($exit_code || 0);
}

__END__
