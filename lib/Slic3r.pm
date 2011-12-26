package Slic3r;

use strict;
use warnings;

our $VERSION = "0.5.8-beta";

our $debug = 0;
sub debugf {
    printf @_ if $debug;
}

use Slic3r::Config;
use Slic3r::ExPolygon;
use Slic3r::Extruder;
use Slic3r::ExtrusionLoop;
use Slic3r::ExtrusionPath;
use Slic3r::ExtrusionPath::Arc;
use Slic3r::ExtrusionPath::Collection;
use Slic3r::Fill;
use Slic3r::Geometry qw(PI);
use Slic3r::Layer;
use Slic3r::Line;
use Slic3r::Perimeter;
use Slic3r::Point;
use Slic3r::Polygon;
use Slic3r::Polyline;
use Slic3r::Polyline::Closed;
use Slic3r::Print;
use Slic3r::Skein;
use Slic3r::STL;
use Slic3r::Surface;
use Slic3r::TriangleMesh;
use Slic3r::TriangleMesh::IntersectionLine;

# output options
our $output_filename_format = '[input_filename_base].gcode';

# printer options
our $nozzle_diameter    = 0.5;
our $print_center       = [100,100];  # object will be centered around this point
our $use_relative_e_distances = 0;
our $extrusion_axis     = 'E';
our $z_offset           = 0;
our $gcode_arcs         = 0;
our $g0                 = 0;
our $gcode_comments     = 0;

# filament options
our $filament_diameter  = 3;    # mm
our $extrusion_multiplier = 1;
our $temperature        = 200;

# speed options
our $travel_speed           = 130;  # mm/sec
our $perimeter_speed        = 30;   # mm/sec
our $small_perimeter_speed  = 30;   # mm/sec
our $infill_speed           = 60;   # mm/sec
our $solid_infill_speed     = 60;   # mm/sec
our $bridge_speed           = 60;   # mm/sec
our $bottom_layer_speed_ratio   = 0.3;

# accuracy options
our $resolution             = 0.00000001;
our $small_perimeter_area   = (5 / $resolution) ** 2;
our $layer_height           = 0.4;
our $first_layer_height_ratio = 1;
our $infill_every_layers    = 1;

# flow options
our $extrusion_width_ratio  = 0;
our $bridge_flow_ratio      = 1;
our $overlap_factor         = 0.5;
our $flow_width;
our $min_flow_spacing;
our $flow_spacing;

# print options
our $perimeters         = 3;
our $solid_layers       = 3;
our $fill_pattern       = 'rectilinear';
our $solid_fill_pattern = 'rectilinear';
our $fill_density       = 0.4;  # 1 = 100%
our $fill_angle         = 45;
our $start_gcode = "G28 ; home all axes";
our $end_gcode = <<"END";
M104 S0 ; turn off temperature
G28 X0  ; home X axis
M84     ; disable motors
END

# retraction options
our $retract_length         = 1;    # mm
our $retract_restart_extra  = 0;    # mm
our $retract_speed          = 30;   # mm/sec
our $retract_before_travel  = 2;    # mm
our $retract_lift           = 0;    # mm

# skirt options
our $skirts             = 1;
our $skirt_distance     = 6;    # mm
our $skirt_height       = 1;    # layers

# transform options
our $scale              = 1;
our $rotate             = 0;
our $duplicate_x        = 1;
our $duplicate_y        = 1;
our $duplicate_distance = 6;    # mm

1;
