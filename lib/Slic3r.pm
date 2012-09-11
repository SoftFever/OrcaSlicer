package Slic3r;

# Copyright holder: Alessandro Ranellucci
# This application is licensed under the GNU Affero General Public License, version 3

use strict;
use warnings;
require v5.10;

our $VERSION = "0.9.3-dev";

our $debug = 0;
sub debugf {
    printf @_ if $debug;
}

# load threads before Moo as required by it
our $have_threads;
BEGIN {
    use Config;
    $have_threads = $Config{useithreads} && eval "use threads; use Thread::Queue; 1";
}

warn "Running Slic3r under Perl >= 5.16 is not supported nor recommended\n"
    if $^V >= v5.16;

use FindBin;
our $var = "$FindBin::Bin/var";

use Moo 0.091009;
use Slic3r::Config;
use Slic3r::ExPolygon;
use Slic3r::Extruder;
use Slic3r::ExtrusionLoop;
use Slic3r::ExtrusionPath;
use Slic3r::ExtrusionPath::Arc;
use Slic3r::ExtrusionPath::Collection;
use Slic3r::Fill;
use Slic3r::Flow;
use Slic3r::Format::AMF;
use Slic3r::Format::OBJ;
use Slic3r::Format::STL;
use Slic3r::GCode;
use Slic3r::Geometry qw(PI);
use Slic3r::Layer;
use Slic3r::Line;
use Slic3r::Model;
use Slic3r::Point;
use Slic3r::Polygon;
use Slic3r::Polyline;
use Slic3r::Print;
use Slic3r::Print::Object;
use Slic3r::Surface;
use Slic3r::TriangleMesh;
eval "use Slic3r::Build";

use constant SCALING_FACTOR         => 0.000001;
use constant RESOLUTION             => 0.01;
use constant OVERLAP_FACTOR         => 0.5;
use constant SMALL_PERIMETER_LENGTH => (6.5 / SCALING_FACTOR) * 2 * PI;

# The following variables hold the objects used throughout the slicing
# process.  They should belong to the Print object, but we are keeping 
# them here because it makes accessing them slightly faster.
our $Config;
our $extruders;
our ($flow, $first_layer_flow, $perimeter_flow, $infill_flow, $support_material_flow);

sub parallelize {
    my %params = @_;
    
    if (!$params{disable} && $Slic3r::have_threads && $Config->threads > 1) {
        my $q = Thread::Queue->new;
        $q->enqueue(@{ $params{items} }, (map undef, 1..$Config->threads));
        
        my $thread_cb = sub { $params{thread_cb}->($q) };
        foreach my $th (map threads->create($thread_cb), 1..$Config->threads) {
            $params{collect_cb}->($th->join);
        }
    } else {
        $params{no_threads_cb}->();
    }
}

1;
