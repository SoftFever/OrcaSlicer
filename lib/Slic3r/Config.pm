package Slic3r::Config;
use strict;
use warnings;
use utf8;

use constant PI => 4 * atan2(1, 1);

# cemetery of old config settings
our @Ignore = qw(duplicate_x duplicate_y multiply_x multiply_y support_material_tool);

our %Groups = (
    print       => [qw(layer_height first_layer_height perimeters randomize_start extra_perimeters solid_layers fill_density fill_angle fill_pattern solid_fill_pattern infill_every_layers perimeter_speed small_perimeter_speed infill_speed solid_infill_speed top_solid_infill_speed bridge_speed travel_speed first_layer_speed skirts skirt_distance skirt_height brim_width support_material support_material_threshold support_material_pattern support_material_pattern support_material_spacing support_material_angle notes complete_objects extruder_clearance_radius extruder_clearance_height gcode_comments output_filename_format post_process extrusion_width first_layer_extrusion_width infill_extrusion_width support_material_extrusion_width bridge_flow_ratio duplicate_distance)],
    filament    => [qw(filament_diameter extrusion_multiplier temperature first_layer_temperature bed_temperature first_layer_bed_temperature cooling min_fan_speed max_fan_speed bridge_fan_speed disable_fan_first_layers fan_always_on fan_below_layer_time slowdown_below_layer_time min_print_speed)],
    printer     => [qw(bed_size print_center z_offset gcode_flavor use_relative_e_distances nozzle_diameter retract_length retract_lift retract_speed retract_restart_extra retract_before_travel start_gcode end_gcode layer_gcode)],
);

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
    },
    'threads' => {
        label   => 'Threads',
        tooltip => 'Threads are used to parallelize long-running tasks. Optimal threads number is slightly above the number of available cores/processors. Beware that more threads consume more memory.',
        sidetext => '(more speed but more memory usage)',
        cli     => 'threads|j=i',
        type    => 'i',
        min     => 1,
        max     => 16,
    },

    # output options
    'output_filename_format' => {
        label   => 'Output filename format',
        tooltip => 'You can use all configuration options as variables inside this template. For example: [layer_height], [fill_density] etc. You can also use [timestamp], [year], [month], [day], [hour], [minute], [second], [version], [input_filename], [input_filename_base].',
        cli     => 'output-filename-format=s',
        type    => 's',
        full_width => 1,
    },

    # printer options
    'print_center' => {
        label   => 'Print center',
        tooltip => 'Enter the G-code coordinates of the point you want to center your print around.',
        sidetext => 'mm',
        cli     => 'print-center=s',
        type    => 'point',
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
    },
    'gcode_flavor' => {
        label   => 'G-code flavor',
        tooltip => 'Some G/M-code commands, including temperature control and others, are not universal. Set this option to your printer\'s firmware to get a compatible output. The "No extrusion" flavor prevents Slic3r from exporting any extrusion value at all.',
        cli     => 'gcode-flavor=s',
        type    => 'select',
        values  => [qw(reprap teacup makerbot mach3 no-extrusion)],
        labels  => ['RepRap (Marlin/Sprinter)', 'Teacup', 'MakerBot', 'Mach3/EMC', 'No extrusion'],
    },
    'use_relative_e_distances' => {
        label   => 'Use relative E distances',
        tooltip => 'If your firmware requires relative E values, check this, otherwise leave it unchecked. Most firmwares use absolute values.',
        cli     => 'use-relative-e-distances!',
        type    => 'bool',
    },
    'extrusion_axis' => {
        label   => 'Extrusion axis',
        tooltip => 'Use this option to set the axis letter associated to your printer\'s extruder (usually E but some printers use A).',
        cli     => 'extrusion-axis=s',
        type    => 's',
    },
    'z_offset' => {
        label   => 'Z offset',
        tooltip => 'This value will be added (or subtracted) from all the Z coordinates in the output G-code. It is used to compensate for bad Z endstop position: for example, if your endstop zero actually leaves the nozzle 0.3mm far from the print bed, set this to -0.3 (or fix your endstop).',
        sidetext => 'mm',
        cli     => 'z-offset=f',
        type    => 'f',
    },
    'gcode_arcs' => {
        label   => 'Use native G-code arcs',
        tooltip => 'This experimental feature tries to detect arcs from segments and generates G2/G3 arc commands instead of multiple straight G1 commands.',
        cli     => 'gcode-arcs!',
        type    => 'bool',
    },
    'g0' => {
        label   => 'Use G0 for travel moves',
        tooltip => 'Only enable this if your firmware supports G0 properly (thus decouples all axes using their maximum speeds instead of synchronizing them). Travel moves and retractions will be combined in single commands, speeding them print up.',
        cli     => 'g0!',
        type    => 'bool',
    },
    'gcode_comments' => {
        label   => 'Verbose G-code',
        tooltip => 'Enable this to get a commented G-code file, with each line explained by a descriptive text. If you print from SD card, the additional weight of the file could make your firmware slow down.',
        cli     => 'gcode-comments!',
        type    => 'bool',
    },
    
    # extruders options
    'nozzle_diameter' => {
        label   => 'Nozzle diameter',
        tooltip => 'This is the diameter of your extruder nozzle (for example: 0.5, 0.35 etc.)',
        cli     => 'nozzle-diameter=f@',
        type    => 'f',
        sidetext => 'mm',
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
    },
    'filament_diameter' => {
        label   => 'Diameter',
        tooltip => 'Enter your filament diameter here. Good precision is required, so use a caliper and do multiple measurements along the filament, then compute the average.',
        sidetext => 'mm',
        cli     => 'filament-diameter=f@',
        type    => 'f',
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
    },
    'extrusion_multiplier' => {
        label   => 'Extrusion multiplier',
        tooltip => 'This factor changes the amount of flow proportionally. You may need to tweak this setting to get nice surface finish and correct single wall widths. Usual values are between 0.9 and 1.1. If you think you need to change this more, check filament diameter and your firmware E steps.',
        cli     => 'extrusion-multiplier=f@',
        type    => 'f',
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
    },
    'temperature' => {
        label   => 'Temperature',
        tooltip => 'Extruder temperature for layers after the first one. Set this to zero to disable temperature control commands in the output.',
        sidetext => '°C',
        cli     => 'temperature=i@',
        type    => 'i',
        max     => 300,
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
    },
    'first_layer_temperature' => {
        label   => 'First layer temperature',
        tooltip => 'Extruder temperature for first layer. If you want to control temperature manually during print, set this to zero to disable temperature control commands in the output file.',
        sidetext => '°C',
        cli     => 'first-layer-temperature=i@',
        type    => 'i',
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
        max     => 300,
    },
    
    # extruder mapping
    'perimeter_extruder' => {
        label   => 'Perimeter extruder',
        cli     => 'perimeter-extruder=i',
        type    => 'i',
        aliases => [qw(perimeters_extruder)],
    },
    'infill_extruder' => {
        label   => 'Infill extruder',
        cli     => 'infill-extruder=i',
        type    => 'i',
    },
    'support_material_extruder' => {
        label   => 'Extruder',
        cli     => 'support-material-extruder=i',
        type    => 'i',
    },
    
    # filament options
    'first_layer_bed_temperature' => {
        label   => 'First layer bed temperature',
        tooltip => 'Heated build plate temperature for the first layer. Set this to zero to disable bed temperature control commands in the output.',
        sidetext => '°C',
        cli     => 'first-layer-bed-temperature=i',
        type    => 'i',
        max     => 300,
    },
    'bed_temperature' => {
        label   => 'Bed Temperature',
        tooltip => 'Bed temperature for layers after the first one. Set this to zero to disable bed temperature control commands in the output.',
        sidetext => '°C',
        cli     => 'bed-temperature=i',
        type    => 'i',
        max     => 300,
    },
    
    # speed options
    'travel_speed' => {
        label   => 'Travel',
        tooltip => 'Speed for travel moves (jumps between distant extrusion points).',
        sidetext => 'mm/s',
        cli     => 'travel-speed=f',
        type    => 'f',
        aliases => [qw(travel_feed_rate)],
    },
    'perimeter_speed' => {
        label   => 'Perimeters',
        tooltip => 'Speed for perimeters (contours, aka vertical shells).',
        sidetext => 'mm/s',
        cli     => 'perimeter-speed=f',
        type    => 'f',
        aliases => [qw(perimeter_feed_rate)],
    },
    'small_perimeter_speed' => {
        label   => 'Small perimeters',
        tooltip => 'This separate setting will affect the speed of perimeters having radius <= 6.5mm (usually holes). If expressed as percentage (for example: 80%) it will be calculated on the perimeters speed setting above.',
        sidetext => 'mm/s or %',
        cli     => 'small-perimeter-speed=s',
        type    => 'f',
        ratio_over => 'perimeter_speed',
    },
    'external_perimeter_speed' => {
        label   => 'External perimeters',
        tooltip => 'This separate setting will affect the speed of external perimeters (the visible ones). If expressed as percentage (for example: 80%) it will be calculated on the perimeters speed setting above.',
        sidetext => 'mm/s or %',
        cli     => 'external-perimeter-speed=s',
        type    => 'f',
        ratio_over => 'perimeter_speed',
    },
    'infill_speed' => {
        label   => 'Infill',
        tooltip => 'Speed for printing the internal fill.',
        sidetext => 'mm/s',
        cli     => 'infill-speed=f',
        type    => 'f',
        aliases => [qw(print_feed_rate infill_feed_rate)],
    },
    'solid_infill_speed' => {
        label   => 'Solid infill',
        tooltip => 'Speed for printing solid regions (top/bottom/internal horizontal shells). This can be expressed as a percentage (for example: 80%) over the default infill speed above.',
        sidetext => 'mm/s or %',
        cli     => 'solid-infill-speed=s',
        type    => 'f',
        ratio_over => 'infill_speed',
        aliases => [qw(solid_infill_feed_rate)],
    },
    'top_solid_infill_speed' => {
        label   => 'Top solid infill',
        tooltip => 'Speed for printing top solid regions. You may want to slow down this to get a nicer surface finish. This can be expressed as a percentage (for example: 80%) over the solid infill speed above.',
        sidetext => 'mm/s or %',
        cli     => 'top-solid-infill-speed=s',
        type    => 'f',
        ratio_over => 'solid_infill_speed',
    },
    'bridge_speed' => {
        label   => 'Bridges',
        tooltip => 'Speed for printing bridges.',
        sidetext => 'mm/s',
        cli     => 'bridge-speed=f',
        type    => 'f',
        aliases => [qw(bridge_feed_rate)],
    },
    'first_layer_speed' => {
        label   => 'First layer speed',
        tooltip => 'If expressed as absolute value in mm/s, this speed will be applied to all the print moves of the first layer, regardless of their type. If expressed as a percentage (for example: 40%) it will scale the default speeds.',
        sidetext => 'mm/s or %',
        cli     => 'first-layer-speed=s',
        type    => 'f',
    },
    
    # acceleration options
    'acceleration' => {
        label   => 'Enable acceleration control',
        cli     => 'acceleration!',
        type    => 'bool',
    },
    'perimeter_acceleration' => {
        label   => 'Perimeters',
        sidetext => 'mm/s²',
        cli     => 'perimeter-acceleration',
        type    => 'f',
    },
    'infill_acceleration' => {
        label   => 'Infill',
        sidetext => 'mm/s²',
        cli     => 'infill-acceleration',
        type    => 'f',
    },
    
    # accuracy options
    'layer_height' => {
        label   => 'Layer height',
        tooltip => 'This setting controls the height (and thus the total number) of the slices/layers. Thinner layers give better accuracy but take more time to print.',
        sidetext => 'mm',
        cli     => 'layer-height=f',
        type    => 'f',
    },
    'first_layer_height' => {
        label   => 'First layer height',
        tooltip => 'When printing with very low layer heights, you might still want to print a thicker bottom layer to improve adhesion and tolerance for non perfect build plates. This can be expressed as an absolute value or as a percentage (for example: 150%) over the default layer height.',
        sidetext => 'mm or %',
        cli     => 'first-layer-height=s',
        type    => 'f',
        ratio_over => 'layer_height',
    },
    'infill_every_layers' => {
        label   => 'Infill every',
        tooltip => 'This feature allows to combine infill and speed up your print by extruding thicker infill layers while preserving thin perimeters, thus accuracy.',
        sidetext => 'layers',
        cli     => 'infill-every-layers=i',
        type    => 'i',
        min     => 1,
    },
    
    # flow options
    'extrusion_width' => {
        label   => 'Default extrusion width',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width. If left to zero, Slic3r calculates a width automatically. If expressed as percentage (for example: 230%) it will be computed over layer height.',
        sidetext => 'mm or % (leave 0 for auto)',
        cli     => 'extrusion-width=s',
        type    => 'f',
    },
    'first_layer_extrusion_width' => {
        label   => 'First layer',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width for first layer. You can use this to force fatter extrudates for better adhesion. If expressed as percentage (for example 120%) if will be computed over the default extrusion width (which could be calculated automatically or set manually using the option above).',
        sidetext => 'mm or % (leave 0 for default)',
        cli     => 'first-layer-extrusion-width=s',
        type    => 'f',
    },
    'perimeter_extrusion_width' => {
        label   => 'Perimeters',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width for perimeters. You may want to use thinner extrudates to get more accurate surfaces. If expressed as percentage (for example 90%) if will be computed over the default extrusion width (which could be calculated automatically or set manually using the option above).',
        sidetext => 'mm or % (leave 0 for default)',
        cli     => 'perimeter-extrusion-width=s',
        type    => 'f',
        aliases => [qw(perimeters_extrusion_width)],
    },
    'infill_extrusion_width' => {
        label   => 'Infill',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width for infill. You may want to use fatter extrudates to speed up the infill and make your parts stronger. If expressed as percentage (for example 90%) if will be computed over the default extrusion width (which could be calculated automatically or set manually using the option above).',
        sidetext => 'mm or % (leave 0 for default)',
        cli     => 'infill-extrusion-width=s',
        type    => 'f',
    },
    'support_material_extrusion_width' => {
        label   => 'Support material',
        tooltip => 'Set this to a non-zero value to set a manual extrusion width for support material. If expressed as percentage (for example 90%) if will be computed over the default extrusion width (which could be calculated automatically or set manually using the option above).',
        sidetext => 'mm or % (leave 0 for default)',
        cli     => 'support-material-extrusion-width=s',
        type    => 'f',
    },
    'bridge_flow_ratio' => {
        label   => 'Bridge flow ratio',
        tooltip => 'This factor affects the amount of plastic for bridging. You can decrease it slightly to pull the extrudates and prevent sagging, although default settings are usually good and you should experiment with cooling (use a fan) before tweaking this.',
        cli     => 'bridge-flow-ratio=f',
        type    => 'f',
    },
    
    # print options
    'perimeters' => {
        label   => 'Perimeters (minimum)',
        tooltip => 'This option sets the number of perimeters to generate for each layer. Note that Slic3r will increase this number automatically when it detects sloping surfaces which benefit from a higher number of perimeters.',
        cli     => 'perimeters=i',
        type    => 'i',
        aliases => [qw(perimeter_offsets)],
    },
    'solid_layers' => {
        label   => 'Solid layers',
        tooltip => 'Number of solid layers to generate on top and bottom.',
        cli     => 'solid-layers=i',
        type    => 'i',
    },
    'fill_pattern' => {
        label   => 'Fill pattern',
        tooltip => 'Fill pattern for general low-density infill.',
        cli     => 'fill-pattern=s',
        type    => 'select',
        values  => [qw(rectilinear line concentric honeycomb hilbertcurve archimedeanchords octagramspiral)],
        labels  => [qw(rectilinear line concentric honeycomb), 'hilbertcurve (slow)', 'archimedeanchords (slow)', 'octagramspiral (slow)'],
    },
    'solid_fill_pattern' => {
        label   => 'Top/bottom fill pattern',
        tooltip => 'Fill pattern for top/bottom infill.',
        cli     => 'solid-fill-pattern=s',
        type    => 'select',
        values  => [qw(rectilinear concentric hilbertcurve archimedeanchords octagramspiral)],
        labels  => [qw(rectilinear concentric), 'hilbertcurve (slow)', 'archimedeanchords (slow)', 'octagramspiral (slow)'],
    },
    'fill_density' => {
        label   => 'Fill density',
        tooltip => 'Density of internal infill, expressed in the range 0 - 1.',
        cli     => 'fill-density=f',
        type    => 'f',
    },
    'fill_angle' => {
        label   => 'Fill angle',
        tooltip => 'Default base angle for infill orientation. Cross-hatching will be applied to this. Bridges will be infilled using the best direction Slic3r can detect, so this setting does not affect them.',
        sidetext => '°',
        cli     => 'fill-angle=i',
        type    => 'i',
        max     => 359,
    },
    'extra_perimeters' => {
        label   => 'Generate extra perimeters when needed',
        cli     => 'extra-perimeters!',
        type    => 'bool',
    },
    'randomize_start' => {
        label   => 'Randomize starting points',
        tooltip => 'Start each layer from a different vertex to prevent plastic build-up on the same corner.',
        cli     => 'randomize-start!',
        type    => 'bool',
    },
    'support_material' => {
        label   => 'Generate support material',
        tooltip => 'Enable support material generation.',
        cli     => 'support-material!',
        type    => 'bool',
    },
    'support_material_threshold' => {
        label   => 'Overhang threshold',
        tooltip => 'Support material will not generated for overhangs whose slope angle is above the given threshold.',
        sidetext => '°',
        cli     => 'support-material-threshold=i',
        type    => 'i',
    },
    'support_material_pattern' => {
        label   => 'Pattern',
        tooltip => 'Pattern used to generate support material.',
        cli     => 'support-material-pattern=s',
        type    => 'select',
        values  => [qw(rectilinear honeycomb)],
        labels  => [qw(rectilinear honeycomb)],
    },
    'support_material_spacing' => {
        label   => 'Pattern spacing',
        tooltip => 'Spacing between support material lines.',
        sidetext => 'mm',
        cli     => 'support-material-spacing=f',
        type    => 'f',
    },
    'support_material_angle' => {
        label   => 'Pattern angle',
        tooltip => 'Use this setting to rotate the support material pattern on the horizontal plane.',
        sidetext => '°',
        cli     => 'support-material-angle=i',
        type    => 'i',
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
        deserialize => sub { [ split /\s*;\s*/, $_[0] ] },
    },
    
    # retraction options
    'retract_length' => {
        label   => 'Length',
        tooltip => 'When retraction is triggered, filament is pulled back by the specified amount (the length is measured on raw filament, before it enters the extruder).',
        sidetext => 'mm (zero to disable)',
        cli     => 'retract-length=f',
        type    => 'f',
    },
    'retract_speed' => {
        label   => 'Speed',
        tooltip => 'The speed for retractions (it only applies to the extruder motor).',
        sidetext => 'mm/s',
        cli     => 'retract-speed=f',
        type    => 'i',
        max     => 1000,
    },
    'retract_restart_extra' => {
        label   => 'Extra length on restart',
        tooltip => 'When the retraction is compensated after the travel move, the extruder will push this additional amount of filament. This setting is rarely needed.',
        sidetext => 'mm',
        cli     => 'retract-restart-extra=f',
        type    => 'f',
    },
    'retract_before_travel' => {
        label   => 'Minimum travel after retraction',
        tooltip => 'Retraction is not triggered when travel moves are shorter than this length.',
        sidetext => 'mm',
        cli     => 'retract-before-travel=f',
        type    => 'f',
    },
    'retract_lift' => {
        label   => 'Lift Z',
        tooltip => 'If you set this to a positive value, Z is quickly raised every time a retraction is triggered.',
        sidetext => 'mm',
        cli     => 'retract-lift=f',
        type    => 'f',
    },
    
    # cooling options
    'cooling' => {
        label   => 'Enable cooling',
        tooltip => 'This flag enables all the cooling features.',
        cli     => 'cooling!',
        type    => 'bool',
    },
    'min_fan_speed' => {
        label   => 'Min fan speed',
        tooltip => 'This setting represents the minimum PWM your fan needs to work.',
        sidetext => '%',
        cli     => 'min-fan-speed=i',
        type    => 'i',
        max     => 1000,
    },
    'max_fan_speed' => {
        label   => 'Max fan speed',
        tooltip => 'This setting represents the maximum speed of your fan.',
        sidetext => '%',
        cli     => 'max-fan-speed=i',
        type    => 'i',
        max     => 1000,
    },
    'bridge_fan_speed' => {
        label   => 'Bridge fan speed',
        tooltip => 'This fan speed is enforced during all bridges.',
        sidetext => '%',
        cli     => 'bridge-fan-speed=i',
        type    => 'i',
        max     => 1000,
    },
    'fan_below_layer_time' => {
        label   => 'Enable fan if layer print time is below',
        tooltip => 'If layer print time is estimated below this number of seconds, fan will be enabled and its speed will be calculated by interpolating the minimum and maximum speeds.',
        sidetext => 'approximate seconds',
        cli     => 'fan-below-layer-time=i',
        type    => 'i',
        max     => 1000,
        width   => 60,
    },
    'slowdown_below_layer_time' => {
        label   => 'Slow down if layer print time is below',
        tooltip => 'If layer print time is estimated below this number of seconds, print moves speed will be scaled down to extend duration to this value.',
        sidetext => 'approximate seconds',
        cli     => 'slowdown-below-layer-time=i',
        type    => 'i',
        max     => 1000,
        width   => 60,
    },
    'min_print_speed' => {
        label   => 'Min print speed',
        tooltip => 'Slic3r will not scale speed down below this speed.',
        sidetext => 'mm/s',
        cli     => 'min-print-speed=f',
        type    => 'i',
        max     => 1000,
    },
    'disable_fan_first_layers' => {
        label   => 'Disable fan for the first',
        tooltip => 'You can set this to a positive value to disable fan at all during the first layers, so that it does not make adhesion worse.',
        sidetext => 'layers',
        cli     => 'disable-fan-first-layers=i',
        type    => 'i',
        max     => 1000,
    },
    'fan_always_on' => {
        label   => 'Keep fan always on',
        tooltip => 'If this is enabled, fan will never be disabled and will be kept running at least at its minimum speed. Useful for PLA, harmful for ABS.',
        cli     => 'fan-always-on!',
        type    => 'bool',
    },
    
    # skirt/brim options
    'skirts' => {
        label   => 'Loops',
        tooltip => 'Number of loops for this skirt, in other words its thickness. Set this to zero to disable skirt.',
        cli     => 'skirts=i',
        type    => 'i',
    },
    'skirt_distance' => {
        label   => 'Distance from object',
        tooltip => 'Distance between skirt and object(s). Set this to zero to attach the skirt to the object(s) and get a brim for better adhesion.',
        sidetext => 'mm',
        cli     => 'skirt-distance=f',
        type    => 'f',
    },
    'skirt_height' => {
        label   => 'Skirt height',
        tooltip => 'Height of skirt expressed in layers. Set this to a tall value to use skirt as a shield against drafts.',
        sidetext => 'layers',
        cli     => 'skirt-height=i',
        type    => 'i',
    },
    'brim_width' => {
        label   => 'Brim width',
        tooltip => 'Horizontal width of the brim that will be printed around each object on the first layer.',
        sidetext => 'mm',
        cli     => 'brim-width=f',
        type    => 'f',
    },
    
    # transform options
    'scale' => {
        label   => 'Scale',
        cli     => 'scale=f',
        type    => 'f',
    },
    'rotate' => {
        label   => 'Rotate',
        sidetext => '°',
        cli     => 'rotate=i',
        type    => 'i',
        max     => 359,
    },
    'duplicate' => {
        label    => 'Copies (autoarrange)',
        cli      => 'duplicate=i',
        type    => 'i',
        min     => 1,
    },
    'bed_size' => {
        label   => 'Bed size',
        tooltip => 'Size of your bed. This is used to adjust the preview in the plater and for auto-arranging parts in it.',
        sidetext => 'mm',
        cli     => 'bed-size=s',
        type    => 'point',
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
    },
    'duplicate_grid' => {
        label   => 'Copies (grid)',
        cli     => 'duplicate-grid=s',
        type    => 'point',
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
    },
    'duplicate_distance' => {
        label   => 'Distance between copies',
        tooltip => 'Distance used for the auto-arrange feature of the plater.',
        sidetext => 'mm',
        cli     => 'duplicate-distance=f',
        type    => 'f',
        aliases => [qw(multiply_distance)],
    },
    
    # sequential printing options
    'complete_objects' => {
        label   => 'Complete individual objects',
        tooltip => 'When printing multiple objects or copies, this feature will complete each object before moving onto next one (and starting it from its bottom layer). This feature is useful to avoid the risk of ruined prints. Slic3r should warn and prevent you from extruder collisions, but beware.',
        cli     => 'complete-objects!',
        type    => 'bool',
    },
    'extruder_clearance_radius' => {
        label   => 'Extruder clearance radius',
        tooltip => 'Set this to the clearance radius around your extruder. If the extruder is not centered, choose the largest value for safety. This setting is used to check for collisions and to display the graphical preview in the plater.',
        sidetext => 'mm',
        cli     => 'extruder-clearance-radius=f',
        type    => 'f',
    },
    'extruder_clearance_height' => {
        label   => 'Extruder clearance height',
        tooltip => 'Set this to the vertical distance between your nozzle tip and (usually) the X carriage rods. In other words, this is the height of the clearance cylinder around your extruder, and it represents the maximum depth the extruder can peek before colliding with other printed objects.',
        sidetext => 'mm',
        cli     => 'extruder-clearance-height=f',
        type    => 'f',
    },
};

sub get {
    my $class = @_ == 2 ? shift : undef;
    my ($opt_key) = @_;
    no strict 'refs';
    my $value = ${"Slic3r::$opt_key"};
    $value = get($Options->{$opt_key}{ratio_over}) * $1/100
        if $Options->{$opt_key}{ratio_over} && $value =~ /^(\d+(?:\.\d+)?)%$/;
    return $value;
}

sub get_raw {
    my $class = @_ == 2 ? shift : undef;
    my ($opt_key) = @_;
    no strict 'refs';
    my $value = ${"Slic3r::$opt_key"};
    return $value;
}

sub set {
    my $class = @_ == 3 ? shift : undef;
    my ($opt_key, $value) = @_;
    no strict 'refs';
    ${"Slic3r::$opt_key"} = $value;
}

sub serialize {
    my $class = @_ == 2 ? shift : undef;
    my ($opt_key) = @_;
    return $Options->{$opt_key}{serialize}
        ? $Options->{$opt_key}{serialize}->(get_raw($opt_key))
        : get_raw($opt_key);
}

sub deserialize {
    my $class = @_ == 3 ? shift : undef;
    my ($opt_key, $value) = @_;
    return $Options->{$opt_key}{deserialize}
        ? set($opt_key, $Options->{$opt_key}{deserialize}->($value))
        : set($opt_key, $value);
}

sub write_ini {
    my $class = shift;
    my ($file, $ini) = @_;
    
    open my $fh, '>', $file;
    binmode $fh, ':utf8';
    printf $fh "# generated by Slic3r $Slic3r::VERSION\n";
    foreach my $category (sort keys %$ini) {
        printf $fh "\n[%s]\n", $category if $category ne '_';
        foreach my $key (sort keys %{$ini->{$category}}) {
            printf $fh "%s = %s\n", $key, $ini->{$category}{$key};
        }
    }
    close $fh;
}

sub save {
    my $class = shift;
    my ($file, $group) = @_;
    
    my $ini = { _ => {} };
    foreach my $opt (sort keys %$Options) {
        next if defined $group && not ($opt ~~ @{$Groups{$group}});
        next if $Options->{$opt}{gui_only};
        my $value = get_raw($opt);
        $value = $Options->{$opt}{serialize}->($value) if $Options->{$opt}{serialize};
        $ini->{_}{$opt} = $value;
    }
    $class->write_ini($file, $ini);
}

sub save_settings {
    my $class = shift;
    my ($file) = @_;
    
    $class->write_ini($file, $Slic3r::Settings);
}

sub setenv {
    my $class = shift;
    foreach my $opt (sort keys %$Options) {
        next if $Options->{$opt}{gui_only};
        my $value = get($opt);
        $value = $Options->{$opt}{serialize}->($value) if $Options->{$opt}{serialize};
        $ENV{"SLIC3R_" . uc $opt} = $value;
    }
}

sub current {
    my $class = shift;
    return { map +($_ => get_raw($_)), sort keys %$Options };
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

sub load_hash {
    my $class = shift;
    my ($hash, $group, $deserialized) = @_;
    
    my %ignore = map { $_ => 1 } @Ignore;
    foreach my $key (sort keys %$hash) {
        next if defined $group && not ($key ~~ @{$Groups{$group}});
        my $val = $hash->{$key};
        
        # handle legacy options
        next if $ignore{$key};
        if ($key =~ /^(extrusion_width|bottom_layer_speed|first_layer_height)_ratio$/) {
            $key = $1;
            $key =~ s/^bottom_layer_speed$/first_layer_speed/;
            $val = $val =~ /^\d+(?:\.\d+)?$/ && $val != 0 ? ($val*100) . "%" : 0;
        }
        
        if (!exists $Options->{$key}) {
            $key = +(grep { $Options->{$_}{aliases} && grep $_ eq $key, @{$Options->{$_}{aliases}} }
                keys %$Options)[0] or warn "Unknown option $key at line $.\n";
        }
        next unless $key;
        my $opt = $Options->{$key};
        set($key, ($opt->{deserialize} && !$deserialized) ? $opt->{deserialize}->($val) : $val);
    }
}

sub load {
    my $class = shift;
    my ($file) = @_;
    
    my $ini = __PACKAGE__->read_ini($file);
    __PACKAGE__->load_hash($ini->{_});
}

sub validate_cli {
    my $class = shift;
    my ($opt) = @_;
    
    for (qw(start end layer)) {
        if (defined $opt->{$_."_gcode"}) {
            if ($opt->{$_."_gcode"} eq "") {
                set($_."_gcode", "");
            } else {
                die "Invalid value for --${_}-gcode: file does not exist"
                    if !-e $opt->{$_."_gcode"};
                open my $fh, "<", $opt->{$_."_gcode"};
                $opt->{$_."_gcode"} = do { local $/; <$fh> };
                close $fh;
            }
        }
    }
}

sub validate {
    my $class = shift;
    
    # -j, --threads
    die "Invalid value for --threads\n"
        if $Slic3r::threads < 1;
    die "Your perl wasn't built with multithread support\n"
        if $Slic3r::threads > 1 && !$Slic3r::have_threads;

    # --layer-height
    die "Invalid value for --layer-height\n"
        if $Slic3r::layer_height <= 0;
    die "--layer-height must be a multiple of print resolution\n"
        if $Slic3r::layer_height / $Slic3r::scaling_factor % 1 != 0;
    
    # --first-layer-height
    die "Invalid value for --first-layer-height\n"
        if $Slic3r::first_layer_height !~ /^(?:\d*(?:\.\d+)?)%?$/;
    $Slic3r::_first_layer_height = Slic3r::Config->get('first_layer_height');
    
    # --filament-diameter
    die "Invalid value for --filament-diameter\n"
        if grep $_ < 1, @$Slic3r::filament_diameter;
    
    # --nozzle-diameter
    die "Invalid value for --nozzle-diameter\n"
        if grep $_ < 0, @$Slic3r::nozzle_diameter;
    die "--layer-height can't be greater than --nozzle-diameter\n"
        if grep $Slic3r::layer_height > $_, @$Slic3r::nozzle_diameter;
    die "First layer height can't be greater than --nozzle-diameter\n"
        if grep $Slic3r::_first_layer_height > $_, @$Slic3r::nozzle_diameter;
    
    # initialize extruder(s)
    $Slic3r::extruders = [];
    for my $t (0, map $_-1, $Slic3r::perimeter_extruder, $Slic3r::infill_extruder, $Slic3r::support_material_extruder) {
        $Slic3r::extruders->[$t] ||= Slic3r::Extruder->new(
            map { $_ => Slic3r::Config->get($_)->[$t] // Slic3r::Config->get($_)->[0] } #/
                qw(nozzle_diameter filament_diameter extrusion_multiplier temperature first_layer_temperature)
        );
    }
    
    # calculate flow
    $Slic3r::flow = $Slic3r::extruders->[0]->make_flow(width => $Slic3r::extrusion_width);
    if ($Slic3r::first_layer_extrusion_width) {
        $Slic3r::first_layer_flow = $Slic3r::extruders->[0]->make_flow(
            layer_height => $Slic3r::_first_layer_height,
            width        => $Slic3r::first_layer_extrusion_width,
        );
    }
    $Slic3r::perimeters_flow = $Slic3r::extruders->[ $Slic3r::perimeter_extruder-1 ]
        ->make_flow(width => $Slic3r::perimeter_extrusion_width || $Slic3r::extrusion_width);
    $Slic3r::infill_flow = $Slic3r::extruders->[ $Slic3r::infill_extruder-1 ]
        ->make_flow(width => $Slic3r::infill_extrusion_width || $Slic3r::extrusion_width);
    $Slic3r::support_material_flow = $Slic3r::extruders->[ $Slic3r::support_material_extruder-1 ]
        ->make_flow(width => $Slic3r::support_material_extrusion_width || $Slic3r::extrusion_width);
    
    Slic3r::debugf "Default flow width = %s (spacing = %s)\n",
        $Slic3r::flow->width, $Slic3r::flow->spacing;
    
    # --perimeters
    die "Invalid value for --perimeters\n"
        if $Slic3r::perimeters < 0;
    
    # --solid-layers
    die "Invalid value for --solid-layers\n"
        if $Slic3r::solid_layers < 0;
    
    # --print-center
    die "Invalid value for --print-center\n"
        if !ref $Slic3r::print_center 
            && (!$Slic3r::print_center || $Slic3r::print_center !~ /^\d+,\d+$/);
    $Slic3r::print_center = [ split /[,x]/, $Slic3r::print_center ]
        if !ref $Slic3r::print_center;
    
    # --fill-pattern
    die "Invalid value for --fill-pattern\n"
        if !exists $Slic3r::Fill::FillTypes{$Slic3r::fill_pattern};
    
    # --solid-fill-pattern
    die "Invalid value for --solid-fill-pattern\n"
        if !exists $Slic3r::Fill::FillTypes{$Slic3r::solid_fill_pattern};
    
    # --fill-density
    die "Invalid value for --fill-density\n"
        if $Slic3r::fill_density < 0 || $Slic3r::fill_density > 1;
    
    # --infill-every-layers
    die "Invalid value for --infill-every-layers\n"
        if $Slic3r::infill_every_layers !~ /^\d+$/ || $Slic3r::infill_every_layers < 1;
    # TODO: this check should be limited to the extruder used for infill
    die "Maximum infill thickness can't exceed nozzle diameter\n"
        if grep $Slic3r::infill_every_layers * $Slic3r::layer_height > $_, @$Slic3r::nozzle_diameter;
    
    # --scale
    die "Invalid value for --scale\n"
        if $Slic3r::scale <= 0;
    
    # --bed-size
    die "Invalid value for --bed-size\n"
        if !ref $Slic3r::bed_size 
            && (!$Slic3r::bed_size || $Slic3r::bed_size !~ /^\d+,\d+$/);
    $Slic3r::bed_size = [ split /[,x]/, $Slic3r::bed_size ]
        if !ref $Slic3r::bed_size;
    
    # --duplicate-grid
    die "Invalid value for --duplicate-grid\n"
        if !ref $Slic3r::duplicate_grid 
            && (!$Slic3r::duplicate_grid || $Slic3r::duplicate_grid !~ /^\d+,\d+$/);
    $Slic3r::duplicate_grid = [ split /[,x]/, $Slic3r::duplicate_grid ]
        if !ref $Slic3r::duplicate_grid;
    
    # --duplicate
    die "Invalid value for --duplicate or --duplicate-grid\n"
        if !$Slic3r::duplicate || $Slic3r::duplicate < 1 || !$Slic3r::duplicate_grid
            || (grep !$_, @$Slic3r::duplicate_grid);
    die "Use either --duplicate or --duplicate-grid (using both doesn't make sense)\n"
        if $Slic3r::duplicate > 1 && $Slic3r::duplicate_grid && (grep $_ && $_ > 1, @$Slic3r::duplicate_grid);
    $Slic3r::duplicate_mode = 'autoarrange' if $Slic3r::duplicate > 1;
    $Slic3r::duplicate_mode = 'grid' if grep $_ && $_ > 1, @$Slic3r::duplicate_grid;
    
    # --skirt-height
    die "Invalid value for --skirt-height\n"
        if $Slic3r::skirt_height < 0;
    
    # --bridge-flow-ratio
    die "Invalid value for --bridge-flow-ratio\n"
        if $Slic3r::bridge_flow_ratio <= 0;
    
    # extruder clearance
    die "Invalid value for --extruder-clearance-radius\n"
        if $Slic3r::extruder_clearance_radius <= 0;
    die "Invalid value for --extruder-clearance-height\n"
        if $Slic3r::extruder_clearance_height <= 0;
    
    $_->first_layer_temperature($_->temperature) for grep !defined $_->first_layer_temperature, @$Slic3r::extruders;
    $Slic3r::first_layer_temperature->[$_] = $Slic3r::extruders->[$_]->first_layer_temperature for 0 .. $#$Slic3r::extruders;  # this is needed to provide a value to the legacy GUI and for config file re-serialization
    $Slic3r::first_layer_bed_temperature //= $Slic3r::bed_temperature;  #/
    
    # G-code flavors
    $Slic3r::extrusion_axis = 'A' if $Slic3r::gcode_flavor eq 'mach3';
    $Slic3r::extrusion_axis = ''  if $Slic3r::gcode_flavor eq 'no-extrusion';
    
    # legacy with existing config files
    $Slic3r::small_perimeter_speed ||= $Slic3r::perimeter_speed;
    $Slic3r::bridge_speed ||= $Slic3r::infill_speed;
    $Slic3r::solid_infill_speed ||= $Slic3r::infill_speed;
    $Slic3r::top_solid_infill_speed ||= $Slic3r::solid_infill_speed;
}

sub replace_options {
    my $class = shift;
    my ($string, $more_variables) = @_;
    
    if ($more_variables) {
        my $variables = join '|', keys %$more_variables;
        $string =~ s/\[($variables)\]/$more_variables->{$1}/eg;
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
    my $options = join '|',
        grep !$Slic3r::Config::Options->{$_}{multiline},
        keys %$Slic3r::Config::Options;
    
    # use that regexp to search and replace option names with option values
    $string =~ s/\[($options)\]/Slic3r::Config->serialize($1)/eg;
    return $string;
}

1;
