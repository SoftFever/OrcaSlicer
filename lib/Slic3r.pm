package Slic3r;

use strict;
use warnings;

use Slic3r::Layer;
use Slic3r::Line;
use Slic3r::Perimeter;
use Slic3r::Point;
use Slic3r::Polyline;
use Slic3r::Polyline::Closed;
use Slic3r::Print;
use Slic3r::STL;
use Slic3r::Surface;

our $layer_height       = 0.4;
our $resolution         = 0.1;
our $perimeter_offsets  = 3;
our $flow_width         = 0.4;  # TODO: verify this is a multiple of $resolution
our $temperature        = 195;

our $flow_rate          = 60;   # mm/sec
our $print_feed_rate    = 60;   # mm/sec
our $travel_feed_rate   = 80;   # mm/sec
our $bottom_layer_speed_ratio = 0.6;

our $use_relative_e_distances = 1;

our $print_center       = [100,100];  # object will be centered around this point

1;
