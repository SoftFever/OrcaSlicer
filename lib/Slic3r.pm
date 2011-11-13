package Slic3r;

use strict;
use warnings;

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
use Slic3r::Geometry;
use Slic3r::Layer;
use Slic3r::Line;
use Slic3r::Line::FacetEdge;
use Slic3r::Perimeter;
use Slic3r::Point;
use Slic3r::Polygon;
use Slic3r::Polyline;
use Slic3r::Polyline::Closed;
use Slic3r::Print;
use Slic3r::Skein;
use Slic3r::STL;
use Slic3r::Surface;
use Slic3r::Surface::Bridge;

# printer options
our $nozzle_diameter    = 0.5;
our $print_center       = [100,100];  # object will be centered around this point
our $use_relative_e_distances = 0;
our $no_extrusion       = 0;
our $z_offset           = 0;
our $gcode_arcs         = 0;

# filament options
our $filament_diameter  = 3;    # mm
our $filament_packing_density = 1;
our $temperature        = 200;

# speed options
our $print_feed_rate            = 60;   # mm/sec
our $travel_feed_rate           = 130;   # mm/sec
our $perimeter_feed_rate        = 30;   # mm/sec
our $bottom_layer_speed_ratio   = 0.3;

# accuracy options
our $resolution             = 0.00000001;
our $layer_height           = 0.4;
our $infill_every_layers    = 1;
our $thickness_ratio        = 1;
our $flow_width;

# print options
our $perimeter_offsets  = 3;
our $solid_layers       = 3;
our $bridge_overlap     = 3;    # mm
our $fill_pattern       = 'rectilinear';
our $solid_fill_pattern = 'rectilinear';
our $fill_density       = 0.4;  # 1 = 100%
our $fill_angle         = 0;
our $start_gcode = "G28 ; home all axes";
our $end_gcode = <<"END";
M104 S0 ; turn off temperature
G28 X0  ; home X axis
M84     ; disable motors
END

# retraction options
our $retract_length         = 1;    # mm
our $retract_restart_extra  = 0;    # mm
our $retract_speed          = 40;   # mm/sec
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
