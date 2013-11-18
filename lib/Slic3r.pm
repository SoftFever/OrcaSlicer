package Slic3r;

# Copyright holder: Alessandro Ranellucci
# This application is licensed under the GNU Affero General Public License, version 3

use strict;
use warnings;
require v5.10;

our $VERSION = "1.0.0RC1";

our $debug = 0;
sub debugf {
    printf @_ if $debug;
}

# load threads before Moo as required by it
our $have_threads;
BEGIN {
    use Config;
    $have_threads = $Config{useithreads} && eval "use threads; use threads::shared; use Thread::Queue; 1";
    
    ### temporarily disable threads if using the broken Moo version
    use Moo;
    $have_threads = 0 if $Moo::VERSION == 1.003000;
}

warn "Running Slic3r under Perl >= 5.16 is not supported nor recommended\n"
    if $^V >= v5.16;

use FindBin;
our $var = "$FindBin::Bin/var";

use Encode;
use Encode::Locale;
use Boost::Geometry::Utils 0.15;
use Moo 1.003001;

use Slic3r::XS;   # import all symbols (constants etc.) before they get parsed
use Slic3r::Config;
use Slic3r::ExPolygon;
use Slic3r::Extruder;
use Slic3r::ExtrusionLoop;
use Slic3r::ExtrusionPath;
use Slic3r::ExtrusionPath::Collection;
use Slic3r::Fill;
use Slic3r::Flow;
use Slic3r::Format::AMF;
use Slic3r::Format::OBJ;
use Slic3r::Format::STL;
use Slic3r::GCode;
use Slic3r::GCode::ArcFitting;
use Slic3r::GCode::CoolingBuffer;
use Slic3r::GCode::Layer;
use Slic3r::GCode::MotionPlanner;
use Slic3r::GCode::Reader;
use Slic3r::GCode::SpiralVase;
use Slic3r::GCode::VibrationLimit;
use Slic3r::Geometry qw(PI);
use Slic3r::Geometry::BoundingBox;
use Slic3r::Geometry::Clipper;
use Slic3r::Layer;
use Slic3r::Layer::Region;
use Slic3r::Line;
use Slic3r::Model;
use Slic3r::Point;
use Slic3r::Polygon;
use Slic3r::Polyline;
use Slic3r::Print;
use Slic3r::Print::Object;
use Slic3r::Print::Region;
use Slic3r::Print::SupportMaterial;
use Slic3r::Surface;
use Slic3r::TriangleMesh;
our $build = eval "use Slic3r::Build; 1";

use constant SCALING_FACTOR         => 0.000001;
use constant RESOLUTION             => 0.0125;
use constant SCALED_RESOLUTION      => RESOLUTION / SCALING_FACTOR;
use constant OVERLAP_FACTOR         => 1;
use constant SMALL_PERIMETER_LENGTH => (6.5 / SCALING_FACTOR) * 2 * PI;
use constant LOOP_CLIPPING_LENGTH_OVER_SPACING      => 0.15;
use constant INFILL_OVERLAP_OVER_SPACING  => 0.45;
use constant EXTERNAL_INFILL_MARGIN => 3;

our $Config;

sub parallelize {
    my %params = @_;
    
    if (!$params{disable} && $Slic3r::have_threads && $Config->threads > 1) {
        my @items = (ref $params{items} eq 'CODE') ? $params{items}->() : @{$params{items}};
        my $q = Thread::Queue->new;
        $q->enqueue(@items, (map undef, 1..$Config->threads));
        
        my $thread_cb = sub {
            $params{thread_cb}->($q);
            Slic3r::thread_cleanup();
            
            # This explicit exit avoids an untrappable 
            # "Attempt to free unreferenced scalar" error
            # triggered on Ubuntu 12.04 32-bit when we're running 
            # from the Wx plater and
            # we're reusing the same plater object more than once.
            # The downside to using this exit is that we can't return
            # any value to the main thread but we're not doing that
            # anymore anyway.
            # collect_cb is completely useless now
            # and should be removed from the codebase.
            threads->exit;
        };
        $params{collect_cb} ||= sub {};
            
        @_ = ();
        foreach my $th (map threads->create($thread_cb), 1..$Config->threads) {
            $params{collect_cb}->($th->join);
        }
    } else {
        $params{no_threads_cb}->();
    }
}

# call this at the very end of each thread (except the main one)
# so that it does not try to free existing objects.
# at that stage, existing objects are only those that we 
# inherited at the thread creation (thus shared) and those 
# that we are returning: destruction will be handled by the
# main thread in both cases.
# reminder: do not destroy inherited objects in other threads,
# as the main thread will still try to destroy them when they
# go out of scope; in other words, if you're undef()'ing an 
# object in a thread, make sure the main thread still holds a
# reference so that it won't be destroyed in thread.
sub thread_cleanup {
    # prevent destruction of shared objects
    no warnings 'redefine';
    *Slic3r::ExPolygon::DESTROY             = sub {};
    *Slic3r::ExPolygon::Collection::DESTROY = sub {};
    *Slic3r::ExtrusionLoop::DESTROY         = sub {};
    *Slic3r::ExtrusionPath::DESTROY         = sub {};
    *Slic3r::ExtrusionPath::Collection::DESTROY = sub {};
    *Slic3r::Line::DESTROY                  = sub {};
    *Slic3r::Point::DESTROY                 = sub {};
    *Slic3r::Polygon::DESTROY               = sub {};
    *Slic3r::Polyline::DESTROY              = sub {};
    *Slic3r::Polyline::Collection::DESTROY  = sub {};
    *Slic3r::Surface::DESTROY               = sub {};
    *Slic3r::Surface::Collection::DESTROY   = sub {};
    *Slic3r::TriangleMesh::DESTROY          = sub {};
    return undef;  # this prevents a "Scalars leaked" warning
}

sub encode_path {
    my ($filename) = @_;
    return encode('locale_fs', $filename);
}

sub open {
    my ($fh, $mode, $filename) = @_;
    return CORE::open $$fh, $mode, encode_path($filename);
}

1;
