# This package loads all the non-GUI Slic3r perl packages.
# In addition, it implements utility functions for file handling and threading.

package Slic3r;

# Copyright holder: Alessandro Ranellucci
# This application is licensed under the GNU Affero General Public License, version 3

use strict;
use warnings;
require v5.10;

our $VERSION = VERSION();
our $FORK_NAME = FORK_NAME();

our $debug = 0;
sub debugf {
    printf @_ if $debug;
}

our $loglevel = 0;

# load threads before Moo as required by it
our $have_threads;
BEGIN {
    # Test, whether the perl was compiled with ithreads support and ithreads actually work.
    use Config;
    $have_threads = $Config{useithreads} && eval "use threads; use threads::shared; use Thread::Queue; 1";
    warn "threads.pm >= 1.96 is required, please update\n" if $have_threads && $threads::VERSION < 1.96;
    
    ### temporarily disable threads if using the broken Moo version
    use Moo;
    $have_threads = 0 if $Moo::VERSION == 1.003000;

    # Disable multi threading completely by an environment value.
    # This is useful for debugging as the Perl debugger does not work
    # in multi-threaded context at all.
    # A good interactive perl debugger is the ActiveState Komodo IDE
    # or the EPIC http://www.epic-ide.org/
    $have_threads = 0 if (defined($ENV{'SLIC3R_SINGLETHREADED'}) && $ENV{'SLIC3R_SINGLETHREADED'} == 1);
    print "Threading disabled\n" if !$have_threads;

    $debug = 1 if (defined($ENV{'SLIC3R_DEBUGOUT'}) && $ENV{'SLIC3R_DEBUGOUT'} == 1);
    print "Debugging output enabled\n" if $debug;
}

warn "Running Slic3r under Perl 5.16 is neither supported nor recommended\n"
    if $^V == v5.16;

use FindBin;
# Path to the images.
our $var = sub { decode_path($FindBin::Bin) . "/var/" . $_[0] };

use Moo 1.003001;

use Slic3r::XS;   # import all symbols (constants etc.) before they get parsed
use Slic3r::Config;
use Slic3r::ExPolygon;
use Slic3r::ExtrusionLoop;
use Slic3r::ExtrusionPath;
use Slic3r::Flow;
use Slic3r::Format::AMF;
use Slic3r::Format::OBJ;
use Slic3r::Format::STL;
use Slic3r::GCode::ArcFitting;
use Slic3r::GCode::CoolingBuffer;
use Slic3r::GCode::MotionPlanner;
use Slic3r::GCode::PressureRegulator;
use Slic3r::GCode::Reader;
use Slic3r::GCode::SpiralVase;
use Slic3r::Geometry qw(PI);
use Slic3r::Geometry::Clipper;
use Slic3r::Layer;
use Slic3r::Line;
use Slic3r::Model;
use Slic3r::Point;
use Slic3r::Polygon;
use Slic3r::Polyline;
use Slic3r::Print;
use Slic3r::Print::GCode;
use Slic3r::Print::Object;
use Slic3r::Print::Simple;
use Slic3r::Print::SupportMaterial;
use Slic3r::Surface;
our $build = eval "use Slic3r::Build; 1";
use Thread::Semaphore;
use Encode::Locale 1.05;
use Encode;
use Unicode::Normalize;

# Scaling between the float and integer coordinates.
# Floats are in mm.
use constant SCALING_FACTOR         => 0.000001;
use constant LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER => 0.15;

# Following constants are used by the infill algorithms and integration tests.
# Resolution to simplify perimeters to. These constants are now used in C++ code only. Better to publish them to Perl from the C++ code.
# use constant RESOLUTION             => 0.0125;
# use constant SCALED_RESOLUTION      => RESOLUTION / SCALING_FACTOR;
use constant INFILL_OVERLAP_OVER_SPACING  => 0.3;

# Keep track of threads we created. Each thread keeps its own list of threads it spwaned.
my @my_threads = ();
my @threads : shared = ();
my $pause_sema = Thread::Semaphore->new;
my $parallel_sema;
my $paused = 0;

# Set the logging level at the Slic3r XS module.
$Slic3r::loglevel = (defined($ENV{'SLIC3R_LOGLEVEL'}) && $ENV{'SLIC3R_LOGLEVEL'} =~ /^[1-9]/) ? $ENV{'SLIC3R_LOGLEVEL'} : 0;
set_logging_level($Slic3r::loglevel);

sub spawn_thread {
    my ($cb) = @_;
    
    my $parent_tid = threads->tid;
    lock @threads;
    
    @_ = ();
    my $thread = threads->create(sub {
        @my_threads = ();
        
        Slic3r::debugf "Starting thread %d (parent: %d)...\n", threads->tid, $parent_tid;
        local $SIG{'KILL'} = sub {
            Slic3r::debugf "Exiting thread %d...\n", threads->tid;
            $parallel_sema->up if $parallel_sema;
            kill_all_threads();
            Slic3r::thread_cleanup();
            threads->exit();
        };
        local $SIG{'STOP'} = sub {
            $pause_sema->down;
            $pause_sema->up;
        };
        $cb->();
    });
    push @my_threads, $thread->tid;
    push @threads, $thread->tid;
    return $thread;
}

# If the threading is enabled, spawn a set of threads.
# Otherwise run the task on the current thread.
# Used for 
#   Slic3r::Print::Object->layers->make_perimeters  : This is a pure C++ function.
#   Slic3r::Print::Object->layers->make_fill        : This is a pure C++ function.
#   Slic3r::Print::SupportMaterial::generate_toolpaths
sub parallelize {
    my %params = @_;
    
    lock @threads;
    if (!$params{disable} && $Slic3r::have_threads && $params{threads} > 1) {
        my @items = (ref $params{items} eq 'CODE') ? $params{items}->() : @{$params{items}};
        my $q = Thread::Queue->new;
        $q->enqueue(@items, (map undef, 1..$params{threads}));
        
        $parallel_sema = Thread::Semaphore->new(-$params{threads});
        $parallel_sema->up;
        my $thread_cb = sub {
            # execute thread callback
            $params{thread_cb}->($q);
            
            # signal the parent thread that we're done
            $parallel_sema->up;
            
            # cleanup before terminating thread
            Slic3r::thread_cleanup();
            
            # This explicit exit avoids an untrappable 
            # "Attempt to free unreferenced scalar" error
            # triggered on Ubuntu 12.04 32-bit when we're running 
            # from the Wx plater and
            # we're reusing the same plater object more than once.
            # The downside to using this exit is that we can't return
            # any value to the main thread but we're not doing that
            # anymore anyway.
            threads->exit;
        };
            
        @_ = ();
        my @my_threads = map spawn_thread($thread_cb), 1..$params{threads};
        
        # We use a semaphore instead of $th->join because joined threads are
        # not listed by threads->list or threads->object anymore, thus can't
        # be signalled.
        $parallel_sema->down;
        $_->detach for @my_threads;
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
    return if !$Slic3r::have_threads;
    
    if (threads->tid == 0) {
        warn "Calling thread_cleanup() from main thread\n";
        return;
    }

    # prevent destruction of shared objects
    no warnings 'redefine';
    *Slic3r::BridgeDetector::DESTROY        = sub {};
    *Slic3r::Config::DESTROY                = sub {};
    *Slic3r::Config::Full::DESTROY          = sub {};
    *Slic3r::Config::GCode::DESTROY         = sub {};
    *Slic3r::Config::Print::DESTROY         = sub {};
    *Slic3r::Config::PrintObject::DESTROY   = sub {};
    *Slic3r::Config::PrintRegion::DESTROY   = sub {};
    *Slic3r::Config::Static::DESTROY        = sub {};
    *Slic3r::ExPolygon::DESTROY             = sub {};
    *Slic3r::ExPolygon::Collection::DESTROY = sub {};
    *Slic3r::Extruder::DESTROY              = sub {};
    *Slic3r::ExtrusionLoop::DESTROY         = sub {};
    *Slic3r::ExtrusionPath::DESTROY         = sub {};
    *Slic3r::ExtrusionPath::Collection::DESTROY = sub {};
    *Slic3r::ExtrusionSimulator::DESTROY    = sub {};
    *Slic3r::Flow::DESTROY                  = sub {};
# Fillers are only being allocated in worker threads, which are not going to be forked.
# Therefore the Filler instances shall be released at the end of the thread.
#    *Slic3r::Filler::DESTROY                = sub {};
    *Slic3r::GCode::DESTROY                 = sub {};
    *Slic3r::GCode::AvoidCrossingPerimeters::DESTROY = sub {};
    *Slic3r::GCode::OozePrevention::DESTROY = sub {};
    *Slic3r::GCode::PlaceholderParser::DESTROY = sub {};
    *Slic3r::GCode::Sender::DESTROY         = sub {};
    *Slic3r::GCode::Wipe::DESTROY           = sub {};
    *Slic3r::GCode::Writer::DESTROY         = sub {};
    *Slic3r::Geometry::BoundingBox::DESTROY = sub {};
    *Slic3r::Geometry::BoundingBoxf::DESTROY = sub {};
    *Slic3r::Geometry::BoundingBoxf3::DESTROY = sub {};
    *Slic3r::Layer::PerimeterGenerator::DESTROY = sub {};
    *Slic3r::Line::DESTROY                  = sub {};
    *Slic3r::Linef3::DESTROY                = sub {};
    *Slic3r::Model::DESTROY                 = sub {};
    *Slic3r::Model::Object::DESTROY         = sub {};
    *Slic3r::Point::DESTROY                 = sub {};
    *Slic3r::Pointf::DESTROY                = sub {};
    *Slic3r::Pointf3::DESTROY               = sub {};
    *Slic3r::Polygon::DESTROY               = sub {};
    *Slic3r::Polyline::DESTROY              = sub {};
    *Slic3r::Polyline::Collection::DESTROY  = sub {};
    *Slic3r::Print::DESTROY                 = sub {};
    *Slic3r::Print::Object::DESTROY         = sub {};
    *Slic3r::Print::Region::DESTROY         = sub {};
    *Slic3r::Surface::DESTROY               = sub {};
    *Slic3r::Surface::Collection::DESTROY   = sub {};
    *Slic3r::Print::SupportMaterial2::DESTROY = sub {};
    *Slic3r::TriangleMesh::DESTROY          = sub {};
    return undef;  # this prevents a "Scalars leaked" warning
}

sub get_running_threads {
    return grep defined($_), map threads->object($_), @_;
}

sub kill_all_threads {
    # if we're the main thread, we send SIGKILL to all the running threads
    if (threads->tid == 0) {
        lock @threads;
        foreach my $thread (get_running_threads(@threads)) {
            Slic3r::debugf "Thread %d killing %d...\n", threads->tid, $thread->tid;
            $thread->kill('KILL');
        }
        
        # unlock semaphore before we block on wait
        # otherwise we'd get a deadlock if threads were paused
        resume_all_threads();
    }
    
    # in any thread we wait for our children
    foreach my $thread (get_running_threads(@my_threads)) {
        Slic3r::debugf "  Thread %d waiting for %d...\n", threads->tid, $thread->tid;
        $thread->join;  # block until threads are killed
        Slic3r::debugf "    Thread %d finished waiting for %d...\n", threads->tid, $thread->tid;
    }
    @my_threads = ();
}

sub pause_all_threads {
    return if $paused;
    lock @threads;
    $paused = 1;
    $pause_sema->down;
    $_->kill('STOP') for get_running_threads(@threads);
}

sub resume_all_threads {
    return unless $paused;
    lock @threads;
    $paused = 0;
    $pause_sema->up;
}

# Convert a Unicode path to a file system locale.
# The encoding is (from Encode::Locale POD):
# Alias       | Windows | Mac OS X     | POSIX
# locale_fs   | ANSI    | UTF-8        | nl_langinfo
# where nl_langinfo is en-US.UTF-8 on a modern Linux as well.
# So this conversion seems to make the most sense on Windows.
sub encode_path {
    my ($path) = @_;
    
    $path = Unicode::Normalize::NFC($path);
    $path = Encode::encode(locale_fs => $path);
    
    return $path;
}

# Convert a path coded by a file system locale to Unicode.
sub decode_path {
    my ($path) = @_;
    
    $path = Encode::decode(locale_fs => $path)
        unless utf8::is_utf8($path);
    
    # The filesystem might force a normalization form (like HFS+ does) so 
    # if we rely on the filename being comparable after the open() + readdir()
    # roundtrip (like when creating and then selecting a preset), we need to 
    # restore our normalization form.
    $path = Unicode::Normalize::NFC($path);
    
    return $path;
}

# Open a file by converting $filename to local file system locales.
sub open {
    my ($fh, $mode, $filename) = @_;
    return CORE::open $$fh, $mode, encode_path($filename);
}

# this package declaration prevents an ugly fatal warning to be emitted when
# spawning a new thread
package GLUquadricObjPtr;

1;
