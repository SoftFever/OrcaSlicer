package Slic3r;

use strict;
use warnings;

our $debug = 0;
sub debugf {
    printf @_ if $debug;
}

use Slic3r::Extruder;
use Slic3r::ExtrusionLoop;
use Slic3r::ExtrusionPath;
use Slic3r::ExtrusionPath::Collection;
use Slic3r::Fill;
use Slic3r::Geometry;
use Slic3r::Layer;
use Slic3r::Line;
use Slic3r::Perimeter;
use Slic3r::Point;
use Slic3r::Polyline;
use Slic3r::Polyline::Closed;
use Slic3r::Print;
use Slic3r::STL;
use Slic3r::Surface;
use Slic3r::Surface::Collection;

# printer options
our $nozzle_diameter    = 0.45;
our $print_center       = [100,100];  # object will be centered around this point
our $use_relative_e_distances = 0;
our $z_offset = 0;

# filament options
our $filament_diameter  = 3;    # mm
our $filament_packing_density = 0.85;

# speed options
our $print_feed_rate            = 60;   # mm/sec
our $travel_feed_rate           = 130;   # mm/sec
our $bottom_layer_speed_ratio   = 0.6;

# accuracy options
our $resolution         = 0.001;
our $layer_height       = 0.4;
our $flow_width;

# print options
our $perimeter_offsets  = 3;
our $solid_layers       = 3;
our $fill_density       = 0.4;  # 1 = 100%
our $fill_angle         = 0;
our $temperature        = 195;

# retraction options
our $retract_length         = 2;    # mm
our $retract_restart_extra  = 0;    # mm
our $retract_speed          = 40;   # mm/sec

# skirt options
our $skirts             = 1;
our $skirt_distance     = 6;    # mm

# transform options
our $scale              = 1;
our $rotate             = 0;
our $multiply_x         = 1;
our $multiply_y         = 1;
our $multiply_distance  = 6;    # mm

1;
