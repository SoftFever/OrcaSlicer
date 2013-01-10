package Slic3r::Config;
use strict;
use warnings;
use utf8;

use List::Util qw(first);

# cemetery of old config settings
our @Ignore = qw(duplicate_x duplicate_y multiply_x multiply_y support_material_tool acceleration);

my $serialize_comma     = sub { join ',', @{$_[0]} };
my $deserialize_comma   = sub { [ split /,/, $_[0] ] };

our $Options = {

    # miscellaneous options
    'notes' => {
        label   => 'Configuration notes',
        tooltip => 'You can put here your personal notes. This text will be added to the G-code header comments.',
        cli     => 'notes=s',
        type    => 's',
        multiline => 1,
        full_width => 1,
        height  => 130,
        serialize   => sub { join '\n', split /\R/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
        default => '',
    },
    'threads' => {
        label   => 'Threads',
        tooltip => 'Threads are used to parallelize long-running tasks. Optimal threads number is slightly above the number of available cores/processors. Beware that more threads consume more memory.',
        sidetext => '(more speed but more memory usage)',
        cli     => 'threads|j=i',
        type    => 'i',
        min     => 1,
        max     => 16,
        default => $Slic3r::have_threads ? 2 : 1,
        readonly => !$Slic3r::have_threads,
    },

    # output options
    'output_filename_format' => {
        label   => 'Output filename format',
        tooltip => 'You can use all configuration options as variables inside this template. For example: [layer_height], [fill_density] etc. You can also use [timestamp], [year], [month], [day], [hour], [minute], [second], [version], [input_filename], [input_filename_base].',
        cli     => 'output-filename-format=s',
        type    => 's',
        full_width => 1,
        default => '[input_filename_base].gcode',
    },

    # printer options
    'print_center' => {
        label   => 'Print center',
        tooltip => 'Enter the G-code coordinates of the point you want to center your print around.',
        sidetext => 'mm',
        cli     => 'print-center=s',
        type    => 'point',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [100,100],
    },
    'gcode_flavor' => {
        label   => 'G-code flavor',
        tooltip => 'Some G/M-code commands, including temperature control and others, are not universal. Set this option to your printer\'s firmware to get a compatible output. The "No extrusion" flavor prevents Slic3r from exporting any extrusion value at all.',
        cli     => 'gcode-flavor=s',
        type    => 'select',
        values  => [qw(reprap teacup makerbot mach3 no-extrusion)],
        labels  => ['RepRap (Marlin/Sprinter)', 'Teacup', 'MakerBot', 'Mach3/EMC', 'No extrusion'],
        default => 'reprap',
    },
    'use_relative_e_distances' => {
        label   => 'Use relative E distances',
        tooltip => 'If your firmware requires relative E values, check this, otherwise leave it unchecked. Most firmwares use absolute values.',
        cli     => 'use-relative-e-distances!',
        type    => 'bool',
        default => 0,
    },
    'extrusion_axis' => {
        label   => 'Extrusion axis',
        tooltip => 'Use this option to set the axis letter associated to your printer\'s extruder (usually E but some printers use A).',
        cli     => 'extrusion-axis=s',
        type    => 's',
        default => 'E',
    },
    'z_offset' => {
        label   => 'Z offset',
        tooltip => 'This value will be added (or subtracted) from all the Z coordinates in the output G-code. It is used to compensate for bad Z endstop position: for example, if your endstop zero actually leaves the nozzle 0.3mm far from the print bed, set this to -0.3 (or fix your endstop).',
        sidetext => 'mm',
        cli     => 'z-offset=f',
        type    => 'f',
        default => 0,
    },
    'gcode_arcs' => {
        label   => 'Use native G-code arcs',
        tooltip => 'This experimental feature tries to detect arcs from segments and generates G2/G3 arc commands instead of multiple straight G1 commands.',
        cli     => 'gcode-arcs!',
        type    => 'bool',
        default => 0,
    },
    'g0' => {
        label   => 'Use G0 for travel moves',
        tooltip => 'Only enable this if your firmware supports G0 properly (thus decouples all axes using their maximum speeds instead of synchronizing them). Travel moves and retractions will be combined in single commands, speeding them print up.',
        cli     => 'g0!',
        type    => 'bool',
        default => 0,
    },
    'gcode_comments' => {
        label   => 'Verbose G-code',
        tooltip => 'Enable this to get a commented G-code file, with each line explained by a descriptive text. If you print from SD card, the additional weight of the file could make your firmware slow down.',
        cli     => 'gcode-comments!',
        type    => 'bool',
        default => 0,
    },
    
    # extruders options
    'extruder_offset' => {
        label   => 'Extruder offset',
        tooltip => 'If your firmware doesn\'t handle the extruder displacement you need the G-code to take it into account. This option lets you specify the displacement of each extruder with respect to the first one. It expects positive coordinates (they will be subtracted from the XY coordinate).',
        sidetext => 'mm',
        cli     => 'extruder-offset=s@',
        type    => 'point',
        serialize   => sub { join ',', map { join 'x', @$_ } @{$_[0]} },
        deserialize => sub { [ map [ split /x/, $_ ], (ref $_[0] eq 'ARRAY') ? @{$_[0]} : (split /,/, $_[0] || '0x0') ] },
        default => [[0,0]],
    },
    'nozzle_diameter' => {
        label   => 'Nozzle diameter',
        tooltip => 'This is the diameter of your extruder nozzle (for example: 0.5, 0.35 etc.)',
        cli     => 'nozzle-diameter=f@',
        type    => 'f',
        sidetext => 'mm',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [0.5],
    },
    'filament_diameter' => {
        label   => 'Diameter',
        tooltip => 'Enter your filament diameter here. Good precision is required, so use a caliper and do multiple measurements along the filament, then compute the average.',
        sidetext => 'mm',
        cli     => 'filament-diameter=f@',
        type    => 'f',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default     => [3],
    },
    'extrusion_multiplier' => {
        label   => 'Extrusion multiplier',
        tooltip => 'This factor changes the amount of flow proportionally. You may need to tweak this setting to get nice surface finish and correct single wall widths. Usual values are between 0.9 and 1.1. If you think you need to change this more, check filament diameter and your firmware E steps.',
        cli     => 'extrusion-multiplier=f@',
        type    => 'f',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [1],
    },
    'temperature' => {
        label   => 'Other layers',
        tooltip => 'Extruder temperature for layers after the first one. Set this to zero to disable temperature control commands in the output.',
        sidetext => '°C',
        cli     => 'temperature=i@',
        type    => 'i',
        max     => 300,
        serialize   => $serialize_comma,
        deserialize => sub { $_[0] ? [ split /,/, $_[0] ] : [0] },
        default => [200],
    },
    'first_layer_temperature' => {
        label   => 'First layer',
        tooltip => 'Extruder temperature for first layer. If you want to control temperature manually during print, set this to zero to disable temperature control commands in the output file.',
        sidetext => '°C',
        cli     => 'first-layer-temperature=i@',
        type    => 'i',
        serialize   => $serialize_comma,
        deserialize => sub { $_[0] ? [ split /,/, $_[0] ] : [0] },
        max     => 300,
        default => [200],
    },
    
    # extruder mapping
    'perimeter_extruder' => {
        label   => 'Perimeter extruder',
        tooltip => 'The extruder to use when printing perimeters.',
        cli     => 'perimeter-extruder=i',
        type    => 'i',
        aliases => [qw(perimeters_extruder)],
        default => 1,
    },
    'infill_extruder' => {
        label   => 'Infill extruder',
        tooltip => 'The extruder to use when printing infill.',
        cli     => 'infill-extruder=i',
        type    => 'i',
        default => 1,
    },
    'support_material_extruder' => {
        label   => 'Support material extruder',
        tooltip => 'The extruder to use when printing support material. This affects brim too.',
        cli     => 'support-material-extruder=i',
        type    => 'i',
        default => 1,
    },
    
    # filament options
    'first_layer_bed_temperature' => {
        label   => 'First layer',
        tooltip => 'Heated build plate temperature for the first layer. Set this to zero to disable bed temperature control commands in the output.',
        sidetext => '°C',
        cli     => 'first-layer-bed-temperature=i',
        type    => 'i',
        max     => 300,
        default => 0,
    },
    'bed_temperature' => {
        label   => 'Other layers',
        tooltip => 'Bed temperature for layers after the first one. Set this to zero to disable bed temperature control commands in the output.',
        sidetext => '°C',
        cli     => 'bed-temperature=i',
        type    => 'i',
        max     => 300,
        default => 0,
    },
    
    # speed options
    'travel_speed' => {
        label   => 'Travel',
        tooltip => 'Speed for travel moves (jumps between distant extrusion points).',
        sidetext => 'mm/s',
        cli     => 'travel-speed=f',
        type    => 'f',
        aliases => [qw(travel_feed_rate)],
        default => 130,
    },
    'perimeter_speed' => {
        label   => 'Perimeters',
        tooltip => 'Speed for perimeters (contours, aka vertical shells).',
        sidetext => 'mm/s',
        cli     => 'perimeter-speed=f',
        type    => 'f',
        aliases => [qw(perimeter_feed_rate)],
        default => 30,
    },
    'small_perimeter_speed' => {
        label   => 'Small perimeters',
        tooltip => 'This separate setting will affect the speed of perimeters having radius <= 6.5mm (usually holes). If expressed as percentage (for example: 80%) it will be calculated on the perimeters speed setting above.',
        sidetext => 'mm/s or %',
        cli     => 'small-perimeter-speed=s',
        type    => 'f',
        ratio_over => 'perimeter_speed',
        default => 30,
    },
    'external_perimeter_speed' => {
        label   => 'External perimeters',
        tooltip => 'This separate setting will affect the speed of external perimeters (the visible ones). If expressed as percentage (for example: 80%) it will be calculated on the perimeters speed setting above.',
        sidetext => 'mm/s or %',
        cli     => 'external-perimeter-speed=s',
        type    => 'f',
        ratio_over => 'perimeter_speed',
        default => '100%',
    },
    'infill_speed' => {
        label   => 'Infill',
        tooltip => 'Speed for printing the internal fill.',
        sidetext => 'mm/s',
        cli     => 'infill-speed=f',
        type    => 'f',
        aliases => [qw(print_feed_rate infill_feed_rate)],
        default => 60,
    },
    'solid_infill_speed' => {
        label   => 'Solid infill',
        tooltip => 'Speed for printing solid regions (top/bottom/internal horizontal shells). This can be expressed as a percentage (for example: 80%) over the default infill speed above.',
        sidetext => 'mm/s or %',
        cli     => 'solid-infill-speed=s',
        type    => 'f',
        ratio_over => 'infill_speed',
        aliases => [qw(solid_infill_feed_rate)],
        default => 60,
    },
    'top_solid_infill_speed' => {
        label   => 'Top solid infill',
        tooltip => 'Speed for printing top solid regions. You may want to slow down this to get a nicer surface finish. This can be expressed as a percentage (for example: 80%) over the solid infill speed above.',
        sidetext => 'mm/s or %',
        cli     => 'top-solid-infill-speed=s',
        type    => 'f',
        ratio_over => 'solid_infill_speed',
        default => 50,
    },
    'support_material_speed' => {
        label   => 'Support material',
        tooltip => 'Speed for printing support material.',
        sidetext => 'mm/s',
        cli     => 'support-material-speed=f',
        type    => 'f',
        default => 60,
    },
    'bridge_speed' => {
        label   => 'Bridges',
        tooltip => 'Speed for printing bridges.',
        sidetext => 'mm/s',
        cli     => 'bridge-speed=f',
        type    => 'f',
        aliases => [qw(bridge_feed_rate)],
        default => 60,
    },
    'gap_fill_speed' => {
        label   => 'Gap fill',
        tooltip => 'Speed for filling small gaps using short zigzag moves. Keep this reasonably low to avoid too much shaking and resonance issues. Set zero to disable gaps filling.',
        sidetext => 'mm/s',
        cli     => 'gap-fill-speed=f',
        type    => 'f',
        default => 20,
    },
    'first_layer_speed' => {
        label   => 'First layer speed',
        tooltip => 'If expressed as absolute value in mm/s, this speed will be applied to all the print moves of the first layer, regardless of their type. If expressed as a percentage (for example: 40%) it will scale the default speeds.',
        sidetext => 'mm/s or %',
        cli     => 'first-layer-speed=s',
        type    => 'f',
        default => '30%',
    },
    
    # acceleration options
    'default_acceleration' => {
        label   => 'Default',
        tooltip => 'This is the acceleration your printer will be reset to after the role-specific acceleration values are used (perimeter/infill). Set zero to prevent resetting acceleration at all.',
        sidetext => 'mm/s²',
        cli     => 'default-acceleration',
        type    => 'f',
        default => 0,
    },
    'perimeter_acceleration' => {
        label   => 'Perimeters',
        tooltip => 'This is the acceleration your printer will use for perimeters. A high value like 9000 usually gives good results if your hardware is up to the job. Set zero to disable acceleration control for perimeters.',
        sidetext => 'mm/s²',
        cli     => 'perimeter-acceleration',
        type    => 'f',
        default => 0,
    },
    'infill_acceleration' => {
        label   => 'Infill',
        tooltip => 'This is the acceleration your printer will use for infill. Set zero to disable acceleration control for infill.',
        sidetext => 'mm/s²',
        cli     => 'infill-acceleration',
        type    => 'f',
        default => 0,
    },
    
    # accuracy options
    'layer_height' => {
        label   => 'Layer height',
        tooltip => 'This setting controls the height (and thus the total number) of the slices/layers. Thinner layers give better accuracy but take more time to print.',
        sidetext => 'mm',
        cli     => 'layer-height=f',
        type    => 'f',
        default => 0.4,
    },
    'first_layer_height' => {
        label   => 'First layer height',
        tooltip => 'When printing with very low layer heights, you might still want to print a thicker bottom layer to improve adhesion and tolerance for non perfect build plates. This can be expressed as an absolute value or as a percentage (for example: 150%) over the default layer height.',
        sidetext => 'mm or %',
        cli     => 'first-layer-height=s',
        type    => 'f',
        ratio_over => 'layer_height',
        default => '100%',
    },
    'infill_every_layers' => {
        label   => 'Infill every',
        tooltip => 'This feature allows to combine infill and speed up your print by extruding thicker infill layers while preserving thin perimeters, thus accuracy.',
        sidetext => 'layers',
        cli     => 'infill-every-layers=i',
        type    => 'i',
        min     => 1,
        default => 1,
    },
    'solid_infill_every_layers' => {
        label   => 'Solid infill every',
        tooltip => 'This feature allows to force a solid layer every given number of layers. Zero to disable.',
        sidetext => 'layers',
        cli     => 'solid-infill-every-layers=i',
        type    => 'i',
        min     => 0,
        default => 0,
    },
    
    # flow options
    'extrusion_width' => {
        label   => 'Default extrusion width',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width. If left to zero, Slic3r calculates a width automatically. If expressed as percentage (for example: 230%) it will be computed over layer height.',
        sidetext => 'mm or % (leave 0 for auto)',
        cli     => 'extrusion-width=s',
        type    => 'f',
        default => 0,
    },
    'first_layer_extrusion_width' => {
        label   => 'First layer',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width for first layer. You can use this to force fatter extrudates for better adhesion. If expressed as percentage (for example 120%) if will be computed over layer height.',
        sidetext => 'mm or % (leave 0 for default)',
        cli     => 'first-layer-extrusion-width=s',
        type    => 'f',
        default => '200%',
    },
    'perimeter_extrusion_width' => {
        label   => 'Perimeters',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width for perimeters. You may want to use thinner extrudates to get more accurate surfaces. If expressed as percentage (for example 90%) if will be computed over layer height.',
        sidetext => 'mm or % (leave 0 for default)',
        cli     => 'perimeter-extrusion-width=s',
        type    => 'f',
        aliases => [qw(perimeters_extrusion_width)],
        default => 0,
    },
    'infill_extrusion_width' => {
        label   => 'Infill',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width for infill. You may want to use fatter extrudates to speed up the infill and make your parts stronger. If expressed as percentage (for example 90%) if will be computed over layer height.',
        sidetext => 'mm or % (leave 0 for default)',
        cli     => 'infill-extrusion-width=s',
        type    => 'f',
        default => 0,
    },
    'support_material_extrusion_width' => {
        label   => 'Support material',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width for support material. If expressed as percentage (for example 90%) if will be computed over layer height.',
        sidetext => 'mm or % (leave 0 for default)',
        cli     => 'support-material-extrusion-width=s',
        type    => 'f',
        default => 0,
    },
    'bridge_flow_ratio' => {
        label   => 'Bridge flow ratio',
        tooltip => 'This factor affects the amount of plastic for bridging. You can decrease it slightly to pull the extrudates and prevent sagging, although default settings are usually good and you should experiment with cooling (use a fan) before tweaking this.',
        cli     => 'bridge-flow-ratio=f',
        type    => 'f',
        default => 1,
    },
    'vibration_limit' => {
        label   => 'Vibration limit',
        tooltip => 'This experimental option will slow down those moves hitting the configured frequency limit. The purpose of limiting vibrations is to avoid mechanical resonance. Set zero to disable.',
        sidetext => 'Hz',
        cli     => 'vibration-limit=f',
        type    => 'f',
        default => 0,
    },
    
    # print options
    'perimeters' => {
        label   => 'Perimeters (minimum)',
        tooltip => 'This option sets the number of perimeters to generate for each layer. Note that Slic3r will increase this number automatically when it detects sloping surfaces which benefit from a higher number of perimeters.',
        cli     => 'perimeters=i',
        type    => 'i',
        aliases => [qw(perimeter_offsets)],
        default => 3,
    },
    'solid_layers' => {
        label   => 'Solid layers',
        tooltip => 'Number of solid layers to generate on top and bottom surfaces.',
        cli     => 'solid-layers=i',
        type    => 'i',
        shortcut => [qw(top_solid_layers bottom_solid_layers)],
    },
    'top_solid_layers' => {
        label   => 'Top',
        tooltip => 'Number of solid layers to generate on top surfaces.',
        cli     => 'top-solid-layers=i',
        type    => 'i',
        default => 3,
    },
    'bottom_solid_layers' => {
        label   => 'Bottom',
        tooltip => 'Number of solid layers to generate on bottom surfaces.',
        cli     => 'bottom-solid-layers=i',
        type    => 'i',
        default => 3,
    },
    'fill_pattern' => {
        label   => 'Fill pattern',
        tooltip => 'Fill pattern for general low-density infill.',
        cli     => 'fill-pattern=s',
        type    => 'select',
        values  => [qw(rectilinear line concentric honeycomb hilbertcurve archimedeanchords octagramspiral)],
        labels  => [qw(rectilinear line concentric honeycomb), 'hilbertcurve (slow)', 'archimedeanchords (slow)', 'octagramspiral (slow)'],
        default => 'rectilinear',
    },
    'solid_fill_pattern' => {
        label   => 'Top/bottom fill pattern',
        tooltip => 'Fill pattern for top/bottom infill.',
        cli     => 'solid-fill-pattern=s',
        type    => 'select',
        values  => [qw(rectilinear concentric hilbertcurve archimedeanchords octagramspiral)],
        labels  => [qw(rectilinear concentric), 'hilbertcurve (slow)', 'archimedeanchords (slow)', 'octagramspiral (slow)'],
        default => 'rectilinear',
    },
    'fill_density' => {
        label   => 'Fill density',
        tooltip => 'Density of internal infill, expressed in the range 0 - 1.',
        cli     => 'fill-density=f',
        type    => 'f',
        default => 0.4,
    },
    'fill_angle' => {
        label   => 'Fill angle',
        tooltip => 'Default base angle for infill orientation. Cross-hatching will be applied to this. Bridges will be infilled using the best direction Slic3r can detect, so this setting does not affect them.',
        sidetext => '°',
        cli     => 'fill-angle=i',
        type    => 'i',
        max     => 359,
        default => 45,
    },
    'solid_infill_below_area' => {
        label   => 'Solid infill threshold area',
        tooltip => 'Force solid infill for regions having a smaller area than the specified threshold.',
        sidetext => 'mm²',
        cli     => 'solid-infill-below-area=f',
        type    => 'f',
        default => 70,
    },
    'extra_perimeters' => {
        label   => 'Generate extra perimeters when needed',
        cli     => 'extra-perimeters!',
        type    => 'bool',
        default => 1,
    },
    'randomize_start' => {
        label   => 'Randomize starting points',
        tooltip => 'Start each layer from a different vertex to prevent plastic build-up on the same corner.',
        cli     => 'randomize-start!',
        type    => 'bool',
        default => 1,
    },
    'only_retract_when_crossing_perimeters' => {
        label   => 'Only retract when crossing perimeters',
        tooltip => 'Disables retraction when travelling between infill paths inside the same island.',
        cli     => 'only-retract-when-crossing-perimeters!',
        type    => 'bool',
        default => 0,
    },
    'support_material' => {
        label   => 'Generate support material',
        tooltip => 'Enable support material generation.',
        cli     => 'support-material!',
        type    => 'bool',
        default => 0,
    },
    'support_material_threshold' => {
        label   => 'Overhang threshold',
        tooltip => 'Support material will not generated for overhangs whose slope angle is above the given threshold. Set to zero for automatic detection.',
        sidetext => '°',
        cli     => 'support-material-threshold=i',
        type    => 'i',
        default => 0,
    },
    'support_material_pattern' => {
        label   => 'Pattern',
        tooltip => 'Pattern used to generate support material.',
        cli     => 'support-material-pattern=s',
        type    => 'select',
        values  => [qw(rectilinear honeycomb)],
        labels  => [qw(rectilinear honeycomb)],
        default => 'rectilinear',
    },
    'support_material_spacing' => {
        label   => 'Pattern spacing',
        tooltip => 'Spacing between support material lines.',
        sidetext => 'mm',
        cli     => 'support-material-spacing=f',
        type    => 'f',
        default => 2.5,
    },
    'support_material_angle' => {
        label   => 'Pattern angle',
        tooltip => 'Use this setting to rotate the support material pattern on the horizontal plane.',
        sidetext => '°',
        cli     => 'support-material-angle=i',
        type    => 'i',
        default => 0,
    },
    'start_gcode' => {
        label   => 'Start G-code',
        tooltip => 'This start procedure is inserted at the beginning of the output file, right after the temperature control commands for extruder and bed. If Slic3r detects M104 or M190 in your custom codes, such commands will not be prepended automatically. Note that you can use placeholder variables for all Slic3r settings, so you can put a "M104 S[first_layer_temperature]" command wherever you want.',
        cli     => 'start-gcode=s',
        type    => 's',
        multiline => 1,
        full_width => 1,
        height  => 120,
        serialize   => sub { join '\n', split /\R+/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
        default => 'G28 ; home all axes',
    },
    'end_gcode' => {
        label   => 'End G-code',
        tooltip => 'This end procedure is inserted at the end of the output file. Note that you can use placeholder variables for all Slic3r settings.',
        cli     => 'end-gcode=s',
        type    => 's',
        multiline => 1,
        full_width => 1,
        height  => 120,
        serialize   => sub { join '\n', split /\R+/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
        default => <<'END',
M104 S0 ; turn off temperature
G28 X0  ; home X axis
M84     ; disable motors
END
    },
    'layer_gcode' => {
        label   => 'Layer change G-code',
        tooltip => 'This custom code is inserted at every layer change, right after the Z move and before the extruder moves to the first layer point. Note that you can use placeholder variables for all Slic3r settings.',
        cli     => 'layer-gcode=s',
        type    => 's',
        multiline => 1,
        full_width => 1,
        height  => 50,
        serialize   => sub { join '\n', split /\R+/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
        default => '',
    },
    'toolchange_gcode' => {
        label   => 'Tool change G-code',
        tooltip => 'This custom code is inserted at every extruder change. Note that you can use placeholder variables for all Slic3r settings as well as [previous_extruder] and [next_extruder].',
        cli     => 'toolchange-gcode=s',
        type    => 's',
        multiline => 1,
        full_width => 1,
        height  => 50,
        serialize   => sub { join '\n', split /\R+/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
        default => '',
    },
    'post_process' => {
        label   => 'Post-processing scripts',
        tooltip => 'If you want to process the output G-code through custom scripts, just list their absolute paths here. Separate multiple scripts with a semicolon. Scripts will be passed the absolute path to the G-code file as the first argument, and they can access the Slic3r config settings by reading environment variables.',
        cli     => 'post-process=s@',
        type    => 's@',
        multiline => 1,
        full_width => 1,
        height  => 60,
        serialize   => sub { join '; ', @{$_[0]} },
        deserialize => sub { [ split /\s*(?:;|\R)\s*/s, $_[0] ] },
        default => [],
    },
    
    # retraction options
    'retract_length' => {
        label   => 'Length',
        tooltip => 'When retraction is triggered, filament is pulled back by the specified amount (the length is measured on raw filament, before it enters the extruder).',
        sidetext => 'mm (zero to disable)',
        cli     => 'retract-length=f@',
        type    => 'f',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [1],
    },
    'retract_speed' => {
        label   => 'Speed',
        tooltip => 'The speed for retractions (it only applies to the extruder motor).',
        sidetext => 'mm/s',
        cli     => 'retract-speed=f@',
        type    => 'i',
        max     => 1000,
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [30],
    },
    'retract_restart_extra' => {
        label   => 'Extra length on restart',
        tooltip => 'When the retraction is compensated after the travel move, the extruder will push this additional amount of filament. This setting is rarely needed.',
        sidetext => 'mm',
        cli     => 'retract-restart-extra=f@',
        type    => 'f',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [0],
    },
    'retract_before_travel' => {
        label   => 'Minimum travel after retraction',
        tooltip => 'Retraction is not triggered when travel moves are shorter than this length.',
        sidetext => 'mm',
        cli     => 'retract-before-travel=f@',
        type    => 'f',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [2],
    },
    'retract_lift' => {
        label   => 'Lift Z',
        tooltip => 'If you set this to a positive value, Z is quickly raised every time a retraction is triggered.',
        sidetext => 'mm',
        cli     => 'retract-lift=f@',
        type    => 'f',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [0],
    },
    'retract_length_toolchange' => {
        label   => 'Length',
        tooltip => 'When retraction is triggered before changing tool, filament is pulled back by the specified amount (the length is measured on raw filament, before it enters the extruder).',
        sidetext => 'mm (zero to disable)',
        cli     => 'retract-length-toolchange=f@',
        type    => 'f',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [3],
    },
    'retract_restart_extra_toolchange' => {
        label   => 'Extra length on restart',
        tooltip => 'When the retraction is compensated after changing tool, the extruder will push this additional amount of filament.',
        sidetext => 'mm',
        cli     => 'retract-restart-extra-toolchange=f@',
        type    => 'f',
        serialize   => $serialize_comma,
        deserialize => $deserialize_comma,
        default => [0],
    },
    
    # cooling options
    'cooling' => {
        label   => 'Enable cooling',
        tooltip => 'This flag enables all the cooling features.',
        cli     => 'cooling!',
        type    => 'bool',
        default => 0,
    },
    'min_fan_speed' => {
        label   => 'Min',
        tooltip => 'This setting represents the minimum PWM your fan needs to work.',
        sidetext => '%',
        cli     => 'min-fan-speed=i',
        type    => 'i',
        max     => 1000,
        default => 35,
    },
    'max_fan_speed' => {
        label   => 'Max',
        tooltip => 'This setting represents the maximum speed of your fan.',
        sidetext => '%',
        cli     => 'max-fan-speed=i',
        type    => 'i',
        max     => 1000,
        default => 100,
    },
    'bridge_fan_speed' => {
        label   => 'Bridges fan speed',
        tooltip => 'This fan speed is enforced during all bridges.',
        sidetext => '%',
        cli     => 'bridge-fan-speed=i',
        type    => 'i',
        max     => 1000,
        default => 100,
    },
    'fan_below_layer_time' => {
        label   => 'Enable fan if layer print time is below',
        tooltip => 'If layer print time is estimated below this number of seconds, fan will be enabled and its speed will be calculated by interpolating the minimum and maximum speeds.',
        sidetext => 'approximate seconds',
        cli     => 'fan-below-layer-time=i',
        type    => 'i',
        max     => 1000,
        width   => 60,
        default => 60,
    },
    'slowdown_below_layer_time' => {
        label   => 'Slow down if layer print time is below',
        tooltip => 'If layer print time is estimated below this number of seconds, print moves speed will be scaled down to extend duration to this value.',
        sidetext => 'approximate seconds',
        cli     => 'slowdown-below-layer-time=i',
        type    => 'i',
        max     => 1000,
        width   => 60,
        default => 15,
    },
    'min_print_speed' => {
        label   => 'Min print speed',
        tooltip => 'Slic3r will not scale speed down below this speed.',
        sidetext => 'mm/s',
        cli     => 'min-print-speed=f',
        type    => 'i',
        max     => 1000,
        default => 10,
    },
    'disable_fan_first_layers' => {
        label   => 'Disable fan for the first',
        tooltip => 'You can set this to a positive value to disable fan at all during the first layers, so that it does not make adhesion worse.',
        sidetext => 'layers',
        cli     => 'disable-fan-first-layers=i',
        type    => 'i',
        max     => 1000,
        default => 1,
    },
    'fan_always_on' => {
        label   => 'Keep fan always on',
        tooltip => 'If this is enabled, fan will never be disabled and will be kept running at least at its minimum speed. Useful for PLA, harmful for ABS.',
        cli     => 'fan-always-on!',
        type    => 'bool',
        default => 0,
    },
    
    # skirt/brim options
    'skirts' => {
        label   => 'Loops',
        tooltip => 'Number of loops for this skirt, in other words its thickness. Set this to zero to disable skirt.',
        cli     => 'skirts=i',
        type    => 'i',
        default => 1,
    },
    'min_skirt_length' => {
        label   => 'Minimum extrusion length',
        tooltip => 'Generate no less than the number of skirt loops required to consume the specified amount of filament on the bottom layer. For multi-extruder machines, this minimum applies to each extruder.',
        sidetext => 'mm',
        cli     => 'min-skirt-length=f',
        type    => 'f',
        default => 0,
        min     => 0,
    },
    'skirt_distance' => {
        label   => 'Distance from object',
        tooltip => 'Distance between skirt and object(s). Set this to zero to attach the skirt to the object(s) and get a brim for better adhesion.',
        sidetext => 'mm',
        cli     => 'skirt-distance=f',
        type    => 'f',
        default => 6,
    },
    'skirt_height' => {
        label   => 'Skirt height',
        tooltip => 'Height of skirt expressed in layers. Set this to a tall value to use skirt as a shield against drafts.',
        sidetext => 'layers',
        cli     => 'skirt-height=i',
        type    => 'i',
        default => 1,
    },
    'brim_width' => {
        label   => 'Brim width',
        tooltip => 'Horizontal width of the brim that will be printed around each object on the first layer.',
        sidetext => 'mm',
        cli     => 'brim-width=f',
        type    => 'f',
        default => 0,
    },
    
    # transform options
    'scale' => {
        label   => 'Scale',
        cli     => 'scale=f',
        type    => 'f',
        default => 1,
    },
    'rotate' => {
        label   => 'Rotate',
        sidetext => '°',
        cli     => 'rotate=i',
        type    => 'i',
        max     => 359,
        default => 0,
    },
    'duplicate' => {
        label   => 'Copies (autoarrange)',
        cli     => 'duplicate=i',
        type    => 'i',
        min     => 1,
        default => 1,
    },
    'bed_size' => {
        label   => 'Bed size',
        tooltip => 'Size of your bed. This is used to adjust the preview in the plater and for auto-arranging parts in it.',
        sidetext => 'mm',
        cli     => 'bed-size=s',
        type    => 'point',
        serialize   => $serialize_comma,
        deserialize => sub { [ split /[,x]/, $_[0] ] },
        default => [200,200],
    },
    'duplicate_grid' => {
        label   => 'Copies (grid)',
        cli     => 'duplicate-grid=s',
        type    => 'point',
        serialize   => $serialize_comma,
        deserialize => sub { [ split /[,x]/, $_[0] ] },
        default => [1,1],
    },
    'duplicate_distance' => {
        label   => 'Distance between copies',
        tooltip => 'Distance used for the auto-arrange feature of the plater.',
        sidetext => 'mm',
        cli     => 'duplicate-distance=f',
        type    => 'f',
        aliases => [qw(multiply_distance)],
        default => 6,
    },
    
    # sequential printing options
    'complete_objects' => {
        label   => 'Complete individual objects',
        tooltip => 'When printing multiple objects or copies, this feature will complete each object before moving onto next one (and starting it from its bottom layer). This feature is useful to avoid the risk of ruined prints. Slic3r should warn and prevent you from extruder collisions, but beware.',
        cli     => 'complete-objects!',
        type    => 'bool',
        default => 0,
    },
    'extruder_clearance_radius' => {
        label   => 'Radius',
        tooltip => 'Set this to the clearance radius around your extruder. If the extruder is not centered, choose the largest value for safety. This setting is used to check for collisions and to display the graphical preview in the plater.',
        sidetext => 'mm',
        cli     => 'extruder-clearance-radius=f',
        type    => 'f',
        default => 20,
    },
    'extruder_clearance_height' => {
        label   => 'Height',
        tooltip => 'Set this to the vertical distance between your nozzle tip and (usually) the X carriage rods. In other words, this is the height of the clearance cylinder around your extruder, and it represents the maximum depth the extruder can peek before colliding with other printed objects.',
        sidetext => 'mm',
        cli     => 'extruder-clearance-height=f',
        type    => 'f',
        default => 20,
    },
};

# generate accessors
{
    no strict 'refs';
    for my $opt_key (keys %$Options) {
        *{$opt_key} = sub { $_[0]{$opt_key} };
    }
}

sub new {
    my $class = shift;
    my %args = @_;
    
    my $self = bless {}, $class;
    $self->apply(%args);
    return $self;
}

sub new_from_defaults {
    my $class = shift;
    my @opt_keys = 
    return $class->new(
        map { $_ => $Options->{$_}{default} }
            grep !$Options->{$_}{shortcut},
            (@_ ? @_ : keys %$Options)
    );
}

sub new_from_cli {
    my $class = shift;
    my %args = @_;
    
    delete $args{$_} for grep !defined $args{$_}, keys %args;
    
    for (qw(start end layer toolchange)) {
        my $opt_key = "${_}_gcode";
        if ($args{$opt_key}) {
            die "Invalid value for --${_}-gcode: file does not exist\n"
                if !-e $args{$opt_key};
            open my $fh, "<", $args{$opt_key}
                or die "Failed to open $args{$opt_key}\n";
            binmode $fh, ':utf8';
            $args{$opt_key} = do { local $/; <$fh> };
            close $fh;
        }
    }
    
    $args{$_} = $Options->{$_}{deserialize}->($args{$_})
        for grep exists $args{$_}, qw(print_center bed_size duplicate_grid extruder_offset);
    
    return $class->new(%args);
}

sub merge {
    my $class = shift;
    my $config = $class->new;
    $config->apply($_) for @_;
    return $config;
}

sub load {
    my $class = shift;
    my ($file) = @_;
    
    my $ini = __PACKAGE__->read_ini($file);
    my $config = __PACKAGE__->new;
    $config->set($_, $ini->{_}{$_}, 1) for keys %{$ini->{_}};
    return $config;
}

sub apply {
    my $self = shift;
    my %args = @_ == 1 ? %{$_[0]} : @_; # accept a single Config object too
    
    $self->set($_, $args{$_}) for keys %args;
}

sub clone {
    my $self = shift;
    my $new = __PACKAGE__->new(%$self);
    $new->{$_} = [@{$new->{$_}}] for grep { ref $new->{$_} eq 'ARRAY' } keys %$new;
    return $new;
}

sub get_value {
    my $self = shift;
    my ($opt_key) = @_;
    
    no strict 'refs';
    my $value = $self->get($opt_key);
    $value = $self->get_value($Options->{$opt_key}{ratio_over}) * $1/100
        if $Options->{$opt_key}{ratio_over} && $value =~ /^(\d+(?:\.\d+)?)%$/;
    return $value;
}

sub get {
    my $self = shift;
    my ($opt_key) = @_;
    
    return $self->{$opt_key};
}

sub set {
    my $self = shift;
    my ($opt_key, $value, $deserialize) = @_;
    
    # handle legacy options
    return if $opt_key ~~ @Ignore;
    if ($opt_key =~ /^(extrusion_width|bottom_layer_speed|first_layer_height)_ratio$/) {
        $opt_key = $1;
        $opt_key =~ s/^bottom_layer_speed$/first_layer_speed/;
        $value = $value =~ /^\d+(?:\.\d+)?$/ && $value != 0 ? ($value*100) . "%" : 0;
    }
    if ($opt_key eq 'threads' && !$Slic3r::have_threads) {
        $value = 1;
    }
    
    if (!exists $Options->{$opt_key}) {
        my @keys = grep { $Options->{$_}{aliases} && grep $_ eq $opt_key, @{$Options->{$_}{aliases}} } keys %$Options;
        if (!@keys) {
            warn "Unknown option $opt_key\n";
            return;
        }
        $opt_key = $keys[0];
    }
    
    # clone arrayrefs
    $value = [@$value] if ref $value eq 'ARRAY';
    
    # deserialize if requested
    $value = $Options->{$opt_key}{deserialize}->($value)
        if $deserialize && $Options->{$opt_key}{deserialize};
    
    $self->{$opt_key} = $value;
    
    if ($Options->{$opt_key}{shortcut}) {
        $self->set($_, $value, $deserialize) for @{$Options->{$opt_key}{shortcut}};
    }
}

sub set_ifndef {
    my $self = shift;
    my ($opt_key, $value, $deserialize) = @_;
    
    $self->set($opt_key, $value, $deserialize)
        if !defined $self->get($opt_key);
}

sub has {
    my $self = shift;
    my ($opt_key) = @_;
    return exists $self->{$opt_key};
}

sub serialize {
    my $self = shift;
    my ($opt_key) = @_;
    
    my $value = $self->get($opt_key);
    $value = $Options->{$opt_key}{serialize}->($value) if $Options->{$opt_key}{serialize};
    return $value;
}

sub save {
    my $self = shift;
    my ($file) = @_;
    
    my $ini = { _ => {} };
    foreach my $opt_key (sort keys %$self) {
        next if $Options->{$opt_key}{shortcut};
        next if $Options->{$opt_key}{gui_only};
        $ini->{_}{$opt_key} = $self->serialize($opt_key);
    }
    __PACKAGE__->write_ini($file, $ini);
}

sub setenv {
    my $self = shift;
    
    foreach my $opt_key (sort keys %$Options) {
        next if $Options->{$opt_key}{gui_only};
        $ENV{"SLIC3R_" . uc $opt_key} = $self->serialize($opt_key);
    }
}

# this method is idempotent by design
sub validate {
    my $self = shift;
    
    # -j, --threads
    die "Invalid value for --threads\n"
        if $self->threads < 1;
    die "Your perl wasn't built with multithread support\n"
        if $self->threads > 1 && !$Slic3r::have_threads;

    # --layer-height
    die "Invalid value for --layer-height\n"
        if $self->layer_height <= 0;
    die "--layer-height must be a multiple of print resolution\n"
        if $self->layer_height / &Slic3r::SCALING_FACTOR % 1 != 0;
    
    # --first-layer-height
    die "Invalid value for --first-layer-height\n"
        if $self->first_layer_height !~ /^(?:\d*(?:\.\d+)?)%?$/;
    
    # --filament-diameter
    die "Invalid value for --filament-diameter\n"
        if grep $_ < 1, @{$self->filament_diameter};
    
    # --nozzle-diameter
    die "Invalid value for --nozzle-diameter\n"
        if grep $_ < 0, @{$self->nozzle_diameter};
    die "--layer-height can't be greater than --nozzle-diameter\n"
        if grep $self->layer_height > $_, @{$self->nozzle_diameter};
    die "First layer height can't be greater than --nozzle-diameter\n"
        if grep $self->get_value('first_layer_height') > $_, @{$self->nozzle_diameter};
    
    # --perimeters
    die "Invalid value for --perimeters\n"
        if $self->perimeters < 0;
    
    # --solid-layers
    die "Invalid value for --solid-layers\n" if defined $self->solid_layers && $self->solid_layers < 0;
    die "Invalid value for --top-solid-layers\n"    if $self->top_solid_layers      < 0;
    die "Invalid value for --bottom-solid-layers\n" if $self->bottom_solid_layers   < 0;
    
    # --print-center
    die "Invalid value for --print-center\n"
        if !ref $self->print_center 
            && (!$self->print_center || $self->print_center !~ /^\d+,\d+$/);
    
    # --fill-pattern
    die "Invalid value for --fill-pattern\n"
        if !first { $_ eq $self->fill_pattern } @{$Options->{fill_pattern}{values}};
    
    # --solid-fill-pattern
    die "Invalid value for --solid-fill-pattern\n"
        if !first { $_ eq $self->solid_fill_pattern } @{$Options->{solid_fill_pattern}{values}};
    
    # --fill-density
    die "Invalid value for --fill-density\n"
        if $self->fill_density < 0 || $self->fill_density > 1;
    die "The selected fill pattern is not supposed to work at 100% density\n"
        if $self->fill_density == 1
            && !first { $_ eq $self->fill_pattern } @{$Options->{solid_fill_pattern}{values}};
    
    # --infill-every-layers
    die "Invalid value for --infill-every-layers\n"
        if $self->infill_every_layers !~ /^\d+$/ || $self->infill_every_layers < 1;
    # TODO: this check should be limited to the extruder used for infill
    die "Maximum infill thickness can't exceed nozzle diameter\n"
        if grep $self->infill_every_layers * $self->layer_height > $_, @{$self->nozzle_diameter};
    
    # --scale
    die "Invalid value for --scale\n"
        if $self->scale <= 0;
    
    # --bed-size
    die "Invalid value for --bed-size\n"
        if !ref $self->bed_size 
            && (!$self->bed_size || $self->bed_size !~ /^\d+,\d+$/);
    
    # --duplicate-grid
    die "Invalid value for --duplicate-grid\n"
        if !ref $self->duplicate_grid 
            && (!$self->duplicate_grid || $self->duplicate_grid !~ /^\d+,\d+$/);
    
    # --duplicate
    die "Invalid value for --duplicate or --duplicate-grid\n"
        if !$self->duplicate || $self->duplicate < 1 || !$self->duplicate_grid
            || (grep !$_, @{$self->duplicate_grid});
    die "Use either --duplicate or --duplicate-grid (using both doesn't make sense)\n"
        if $self->duplicate > 1 && $self->duplicate_grid && (grep $_ && $_ > 1, @{$self->duplicate_grid});
    
    # --skirt-height
    die "Invalid value for --skirt-height\n"
        if $self->skirt_height < 0;
    
    # --bridge-flow-ratio
    die "Invalid value for --bridge-flow-ratio\n"
        if $self->bridge_flow_ratio <= 0;
    
    # extruder clearance
    die "Invalid value for --extruder-clearance-radius\n"
        if $self->extruder_clearance_radius <= 0;
    die "Invalid value for --extruder-clearance-height\n"
        if $self->extruder_clearance_height <= 0;
}

sub replace_options {
    my $self = shift;
    my ($string, $more_variables) = @_;
    
    $more_variables ||= {};
    $more_variables->{$_} = $ENV{$_} for grep /^SLIC3R_/, keys %ENV;
    {
        my $variables_regex = join '|', keys %$more_variables;
        $string =~ s/\[($variables_regex)\]/$more_variables->{$1}/eg;
    }
    
    my @lt = localtime; $lt[5] += 1900; $lt[4] += 1;
    $string =~ s/\[timestamp\]/sprintf '%04d%02d%02d-%02d%02d%02d', @lt[5,4,3,2,1,0]/egx;
    $string =~ s/\[year\]/$lt[5]/eg;
    $string =~ s/\[month\]/$lt[4]/eg;
    $string =~ s/\[day\]/$lt[3]/eg;
    $string =~ s/\[hour\]/$lt[2]/eg;
    $string =~ s/\[minute\]/$lt[1]/eg;
    $string =~ s/\[second\]/$lt[0]/eg;
    $string =~ s/\[version\]/$Slic3r::VERSION/eg;
    
    # build a regexp to match the available options
    my @options = grep !$Slic3r::Config::Options->{$_}{multiline},
        grep $self->has($_),
        keys %{$Slic3r::Config::Options};
    my $options_regex = join '|', @options;
    
    # use that regexp to search and replace option names with option values
    $string =~ s/\[($options_regex)\]/$self->serialize($1)/eg;
    foreach my $opt_key (grep ref $self->$_ eq 'ARRAY', @options) {
        my $value = $self->$opt_key;
        $string =~ s/\[${opt_key}_${_}\]/$value->[$_]/eg for 0 .. $#$value;
        if ($Options->{$opt_key}{type} eq 'point') {
            $string =~ s/\[${opt_key}_X\]/$value->[0]/eg;
            $string =~ s/\[${opt_key}_Y\]/$value->[1]/eg;
        }
    }
    return $string;
}

# min object distance is max(duplicate_distance, clearance_radius)
sub min_object_distance {
    my $self = shift;
    
    return ($self->complete_objects && $self->extruder_clearance_radius > $self->duplicate_distance)
        ? $self->extruder_clearance_radius
        : $self->duplicate_distance;
}

# CLASS METHODS:

sub write_ini {
    my $class = shift;
    my ($file, $ini) = @_;
    
    open my $fh, '>', $file;
    binmode $fh, ':utf8';
    my $localtime = localtime;
    printf $fh "# generated by Slic3r $Slic3r::VERSION on %s\n", "$localtime";
    foreach my $category (sort keys %$ini) {
        printf $fh "\n[%s]\n", $category if $category ne '_';
        foreach my $key (sort keys %{$ini->{$category}}) {
            printf $fh "%s = %s\n", $key, $ini->{$category}{$key};
        }
    }
    close $fh;
}

sub read_ini {
    my $class = shift;
    my ($file) = @_;
    
    local $/ = "\n";
    open my $fh, '<', $file;
    binmode $fh, ':utf8';
    
    my $ini = { _ => {} };
    my $category = '_';
    while (my $_ = <$fh>) {
        s/\R+$//;
        next if /^\s+/;
        next if /^$/;
        next if /^\s*#/;
        if (/^\[(\w+)\]$/) {
            $category = $1;
            next;
        }
        /^(\w+) = (.*)/ or die "Unreadable configuration file (invalid data at line $.)\n";
        $ini->{$category}{$1} = $2;
    }
    close $fh;
    
    return $ini;
}

1;
