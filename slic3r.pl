#!/usr/bin/perl

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/lib";
}

use Getopt::Long qw(:config no_auto_abbrev);
use Slic3r;
$|++;

my %opt = ();
my %cli_options = ();
{
    my %options = (
        'help'                  => sub { usage() },
        
        'debug'                 => \$Slic3r::debug,
        'gui'                   => \$opt{gui},
        'o|output=s'            => \$opt{output},
        
        'save=s'                => \$opt{save},
        'load=s@'               => \$opt{load},
        'ignore-nonexistent-config' => \$opt{ignore_nonexistent_config},
        'export-svg'            => \$opt{export_svg},
        'merge|m'               => \$opt{merge},
    );
    foreach my $opt_key (keys %$Slic3r::Config::Options) {
        my $opt = $Slic3r::Config::Options->{$opt_key};
        my $cli = $opt->{cli} or next;
        if ($cli =~ /-/) {
            # allow alternative options with '_' in place of '-'
            $cli = $opt_key.'|'.$cli;
        }
        $options{ $cli } = \$cli_options{$opt_key};
    }
    
    GetOptions(%options) or usage(1);
}

# load configuration
if ($opt{load}) {
    foreach my $configfile (@{$opt{load}}) {
        if (-e $configfile) {
            Slic3r::Config->load($configfile);
        } elsif (-e "$FindBin::Bin/$configfile") {
            printf STDERR "Loading $FindBin::Bin/$configfile\n";
            Slic3r::Config->load("$FindBin::Bin/$configfile");
        } else {
            $opt{ignore_nonexistent_config} or die "Cannot find specified configuration file ($configfile).\n";
        }
    }
}

# validate command line options
Slic3r::Config->validate_cli(\%cli_options);

# apply command line options
Slic3r::Config->set($_ => $cli_options{$_})
    for grep defined $cli_options{$_}, keys %cli_options;

# validate configuration
Slic3r::Config->validate;

# save configuration
Slic3r::Config->save($opt{save}) if $opt{save};

# start GUI
if (!@ARGV && !$opt{save} && eval "require Slic3r::GUI; 1") {
    no warnings 'once';
    $Slic3r::GUI::SkeinPanel::last_config = $opt{load} ? $opt{load}[0] : undef;
    Slic3r::GUI->new->MainLoop;
    exit;
}
die $@ if $@ && $opt{gui};

if (@ARGV) {
    while (my $input_file = shift @ARGV) {
        my $print = Slic3r::Print->new;
        $print->add_object_from_file($input_file);
        if ($opt{merge}) {
            $print->add_object_from_file($_) for splice @ARGV, 0;
        }
        $print->duplicate;
        $print->arrange_objects if @{$print->objects} > 1;
        $print->validate;
        my %params = (
            output_file => $opt{output},
            status_cb   => sub {
                my ($percent, $message) = @_;
                printf "=> $message\n";
            },
        );
        if ($opt{export_svg}) {
            $print->export_svg(%params);
        } else {
            $print->export_gcode(%params);
        }
    }
} else {
    usage(1) unless $opt{save};
}

sub usage {
    my ($exit_code) = @_;
    
    my $j = '';
    if ($Slic3r::have_threads) {
        $j = <<"EOF";
    -j, --threads <num> Number of threads to use (1+, default: $Slic3r::threads)
EOF
    }
    
    print <<"EOF";
Slic3r $Slic3r::VERSION is a STL-to-GCODE translator for RepRap 3D printers
written by Alessandro Ranellucci <aar\@cpan.org> - http://slic3r.org/

Usage: slic3r.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    --save <file>       Save configuration to the specified file
    --load <file>       Load configuration from the specified file. It can be used 
                        more than once to load options from multiple files.
    -o, --output <file> File to output gcode to (by default, the file will be saved
                        into the same directory as the input file using the 
                        --output-filename-format to generate the filename)
$j
  Output options:
    --output-filename-format
                        Output file name format; all config options enclosed in brackets
                        will be replaced by their values, as well as [input_filename_base]
                        and [input_filename] (default: $Slic3r::output_filename_format)
    --post-process      Generated G-code will be processed with the supplied script;
                        call this more than once to process through multiple scripts.
    --export-svg        Export a SVG file containing slices instead of G-code.
    -m, --merge         If multiple files are supplied, they will be composed into a single 
                        print rather than processed individually.
  
  Printer options:
    --nozzle-diameter   Diameter of nozzle in mm (default: $Slic3r::nozzle_diameter)
    --print-center      Coordinates in mm of the point to center the print around 
                        (default: $Slic3r::print_center->[0],$Slic3r::print_center->[1])
    --z-offset          Additional height in mm to add to vertical coordinates
                        (+/-, default: $Slic3r::z_offset)
    --gcode-flavor      The type of G-code to generate (reprap/teacup/makerbot/mach3/no-extrusion,
                        default: $Slic3r::gcode_flavor)
    --use-relative-e-distances Enable this to get relative E values
    --gcode-arcs        Use G2/G3 commands for native arcs (experimental, not supported
                        by all firmwares)
    --g0                Use G0 commands for retraction (experimental, not supported by all
                        firmwares)
    --gcode-comments    Make G-code verbose by adding comments (default: no)
    
  Filament options:
    --filament-diameter Diameter in mm of your raw filament (default: $Slic3r::filament_diameter)
    --extrusion-multiplier
                        Change this to alter the amount of plastic extruded. There should be
                        very little need to change this value, which is only useful to 
                        compensate for filament packing (default: $Slic3r::extrusion_multiplier)
    --temperature       Extrusion temperature in degree Celsius, set 0 to disable (default: $Slic3r::temperature)
    --first-layer-temperature Extrusion temperature for the first layer, in degree Celsius,
                        set 0 to disable (default: same as --temperature)
    --bed-temperature   Heated bed temperature in degree Celsius, set 0 to disable (default: $Slic3r::temperature)
    --first-layer-bed-temperature Heated bed temperature for the first layer, in degree Celsius,
                        set 0 to disable (default: same as --bed-temperature)
    
  Speed options:
    --travel-speed      Speed of non-print moves in mm/s (default: $Slic3r::travel_speed)
    --perimeter-speed   Speed of print moves for perimeters in mm/s (default: $Slic3r::perimeter_speed)
    --small-perimeter-speed
                        Speed of print moves for small perimeters in mm/s (default: $Slic3r::small_perimeter_speed)
    --infill-speed      Speed of print moves in mm/s (default: $Slic3r::infill_speed)
    --solid-infill-speed Speed of print moves for solid surfaces in mm/s (default: $Slic3r::solid_infill_speed)
    --bridge-speed      Speed of bridge print moves in mm/s (default: $Slic3r::bridge_speed)
    --bottom-layer-speed-ratio
                        Factor to increase/decrease speeds on bottom 
                        layer by (default: $Slic3r::bottom_layer_speed_ratio)
    
  Accuracy options:
    --layer-height      Layer height in mm (default: $Slic3r::layer_height)
    --first-layer-height-ratio
                        Multiplication factor for the height to slice and print the first
                        layer with (> 0, default: $Slic3r::first_layer_height_ratio)
    --infill-every-layers
                        Infill every N layers (default: $Slic3r::infill_every_layers)
  
  Print options:
    --perimeters        Number of perimeters/horizontal skins (range: 0+, default: $Slic3r::perimeters)
    --solid-layers      Number of solid layers to do for top/bottom surfaces
                        (range: 1+, default: $Slic3r::solid_layers)
    --fill-density      Infill density (range: 0-1, default: $Slic3r::fill_density)
    --fill-angle        Infill angle in degrees (range: 0-90, default: $Slic3r::fill_angle)
    --fill-pattern      Pattern to use to fill non-solid layers (default: $Slic3r::fill_pattern)
    --solid-fill-pattern Pattern to use to fill solid layers (default: $Slic3r::solid_fill_pattern)
    --start-gcode       Load initial G-code from the supplied file. This will overwrite
                        the default command (home all axes [G28]).
    --end-gcode         Load final G-code from the supplied file. This will overwrite 
                        the default commands (turn off temperature [M104 S0],
                        home X axis [G28 X], disable motors [M84]).
    --layer-gcode       Load layer-change G-code from the supplied file (default: nothing).
    --support-material  Generate support material for overhangs
    --randomize-start   Randomize starting point across layers (default: yes)
  
   Retraction options:
    --retract-length    Length of retraction in mm when pausing extrusion 
                        (default: $Slic3r::retract_length)
    --retract-speed     Speed for retraction in mm/s (default: $Slic3r::retract_speed)
    --retract-restart-extra
                        Additional amount of filament in mm to push after
                        compensating retraction (default: $Slic3r::retract_restart_extra)
    --retract-before-travel
                        Only retract before travel moves of this length in mm (default: $Slic3r::retract_before_travel)
    --retract-lift      Lift Z by the given distance in mm when retracting (default: $Slic3r::retract_lift)
   
   Cooling options:
    --cooling           Enable fan and cooling control
    --min-fan-speed     Minimum fan speed (default: $Slic3r::min_fan_speed%)
    --max-fan-speed     Maximum fan speed (default: $Slic3r::max_fan_speed%)
    --bridge-fan-speed  Fan speed to use when bridging (default: $Slic3r::bridge_fan_speed%)
    --fan-below-layer-time Enable fan if layer print time is below this approximate number 
                        of seconds (default: $Slic3r::fan_below_layer_time)
    --slowdown-below-layer-time Slow down if layer print time is below this approximate number
                        of seconds (default: $Slic3r::slowdown_below_layer_time)
    --min-print-speed   Minimum print speed speed (mm/s, default: $Slic3r::min_print_speed)
    --disable-fan-first-layers Disable fan for the first N layers (default: $Slic3r::disable_fan_first_layers)
    --fan-always-on     Keep fan always on at min fan speed, even for layers that don't need
                        cooling
   
   Skirt options:
    --skirts            Number of skirts to draw (0+, default: $Slic3r::skirts)
    --skirt-distance    Distance in mm between innermost skirt and object 
                        (default: $Slic3r::skirt_distance)
    --skirt-height      Height of skirts to draw (expressed in layers, 0+, default: $Slic3r::skirt_height)
   
   Transform options:
    --scale             Factor for scaling input object (default: $Slic3r::scale)
    --rotate            Rotation angle in degrees (0-360, default: $Slic3r::rotate)
    --duplicate         Number of items with auto-arrange (1+, default: $Slic3r::duplicate)
    --bed-size          Bed size, only used for auto-arrange (mm, default: $Slic3r::bed_size->[0],$Slic3r::bed_size->[1])
    --duplicate-grid    Number of items with grid arrangement (default: $Slic3r::duplicate_grid->[0],$Slic3r::duplicate_grid->[1])
    --duplicate-distance Distance in mm between copies (default: $Slic3r::duplicate_distance)
   
   Sequential printing options:
    --complete-objects  When printing multiple objects and/or copies, complete each one before
                        starting the next one; watch out for extruder collisions (default: no)
    --extruder-clearance-radius Radius in mm above which extruder won't collide with anything
                        (default: $Slic3r::extruder_clearance_radius)
    --extruder-clearance-height Maximum vertical extruder depth; i.e. vertical distance from 
                        extruder tip and carriage bottom (default: $Slic3r::extruder_clearance_height)
   
   Miscellaneous options:
    --notes             Notes to be added as comments to the output file
  
   Flow options (advanced):
    --extrusion-width-ratio
                        Calculate the extrusion width as the layer height multiplied by
                        this value (> 0, default: calculated automatically)
    --bridge-flow-ratio Multiplier for extrusion when bridging (> 0, default: $Slic3r::bridge_flow_ratio)
    
EOF
    exit ($exit_code || 0);
}

__END__
