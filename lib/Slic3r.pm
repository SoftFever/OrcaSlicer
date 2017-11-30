# This package loads all the non-GUI Slic3r perl packages.
# In addition, it implements utility functions for file handling and threading.

package Slic3r;

# Copyright holder: Alessandro Ranellucci
# This application is licensed under the GNU Affero General Public License, version 3

use strict;
use warnings;
use Config;
require v5.10;

our $VERSION = VERSION();
our $BUILD = BUILD();
our $FORK_NAME = FORK_NAME();

our $debug = 0;
sub debugf {
    printf @_ if $debug;
}

our $loglevel = 0;

# load threads before Moo as required by it
BEGIN {
    # Test, whether the perl was compiled with ithreads support and ithreads actually work.
    use Config;
    use Moo;
    my $have_threads = $Config{useithreads} && eval "use threads; use threads::shared; use Thread::Queue; 1";
    die "Slic3r Prusa Edition requires working Perl threads.\n" if ! $have_threads;
    die "threads.pm >= 1.96 is required, please update\n" if $threads::VERSION < 1.96;
    die "Perl threading is broken with this Moo version: " . $Moo::VERSION . "\n" if $Moo::VERSION == 1.003000;
    $debug = 1 if (defined($ENV{'SLIC3R_DEBUGOUT'}) && $ENV{'SLIC3R_DEBUGOUT'} == 1);
    print "Debugging output enabled\n" if $debug;
}

warn "Running Slic3r under Perl 5.16 is neither supported nor recommended\n"
    if $^V == v5.16;

use FindBin;

# Let the XS module know where the GUI resources reside.
set_var_dir(decode_path($FindBin::Bin) . "/var");

use Moo 1.003001;

use Slic3r::XS;   # import all symbols (constants etc.) before they get parsed
use Slic3r::Config;
use Slic3r::ExPolygon;
use Slic3r::ExtrusionLoop;
use Slic3r::ExtrusionPath;
use Slic3r::Flow;
use Slic3r::GCode::Reader;
use Slic3r::Geometry::Clipper;
use Slic3r::Layer;
use Slic3r::Line;
use Slic3r::Model;
use Slic3r::Point;
use Slic3r::Polygon;
use Slic3r::Polyline;
use Slic3r::Print;
use Slic3r::Print::Object;
use Slic3r::Print::Simple;
use Slic3r::Surface;
our $build = eval "use Slic3r::Build; 1";
use Thread::Semaphore;

# Scaling between the float and integer coordinates.
# Floats are in mm.
use constant SCALING_FACTOR         => 0.000001;

# Keep track of threads we created. Perl worker threads shall not create further threads.
my @threads = ();
my $pause_sema = Thread::Semaphore->new;
my $paused = 0;

# Set the logging level at the Slic3r XS module.
$Slic3r::loglevel = (defined($ENV{'SLIC3R_LOGLEVEL'}) && $ENV{'SLIC3R_LOGLEVEL'} =~ /^[1-9]/) ? $ENV{'SLIC3R_LOGLEVEL'} : 0;
set_logging_level($Slic3r::loglevel);

sub spawn_thread {
    my ($cb) = @_;
    @_ = ();
    my $thread = threads->create(sub {
        Slic3r::debugf "Starting thread %d...\n", threads->tid;
        local $SIG{'KILL'} = sub {
            Slic3r::debugf "Exiting thread %d...\n", threads->tid;
            Slic3r::thread_cleanup();
            threads->exit();
        };
        local $SIG{'STOP'} = sub {
            $pause_sema->down;
            $pause_sema->up;
        };
        $cb->();
    });
    push @threads, $thread->tid;
    return $thread;
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
    *Slic3r::ExtrusionLoop::DESTROY         = sub {};
    *Slic3r::ExtrusionMultiPath::DESTROY    = sub {};
    *Slic3r::ExtrusionPath::DESTROY         = sub {};
    *Slic3r::ExtrusionPath::Collection::DESTROY = sub {};
    *Slic3r::ExtrusionSimulator::DESTROY    = sub {};
    *Slic3r::Flow::DESTROY                  = sub {};
    *Slic3r::GCode::DESTROY                 = sub {};
    *Slic3r::GCode::PlaceholderParser::DESTROY = sub {};
    *Slic3r::GCode::Sender::DESTROY         = sub {};
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
    *Slic3r::GUI::AppConfig::DESTROY        = sub {};
    *Slic3r::GUI::PresetBundle::DESTROY     = sub {};
    return undef;  # this prevents a "Scalars leaked" warning
}

sub _get_running_threads {
    return grep defined($_), map threads->object($_), @threads;
}

sub kill_all_threads {
    # Send SIGKILL to all the running threads to let them die.
    foreach my $thread (_get_running_threads) {
        Slic3r::debugf "Thread %d killing %d...\n", threads->tid, $thread->tid;
        $thread->kill('KILL');
    }
    # unlock semaphore before we block on wait
    # otherwise we'd get a deadlock if threads were paused
    resume_all_threads();
    # in any thread we wait for our children
    foreach my $thread (_get_running_threads) {
        Slic3r::debugf "  Thread %d waiting for %d...\n", threads->tid, $thread->tid;
        $thread->join;  # block until threads are killed
        Slic3r::debugf "    Thread %d finished waiting for %d...\n", threads->tid, $thread->tid;
    }
    @threads = ();
}

sub pause_all_threads {
    return if $paused;
    $paused = 1;
    $pause_sema->down;
    $_->kill('STOP') for _get_running_threads;
}

sub resume_all_threads {
    return unless $paused;
    $paused = 0;
    $pause_sema->up;
}

# Open a file by converting $filename to local file system locales.
sub open {
    my ($fh, $mode, $filename) = @_;
    return CORE::open $$fh, $mode, encode_path($filename);
}

sub tags {
    my ($format) = @_;
    $format //= '';
    my %tags;
    # End of line
    $tags{eol}     = ($format eq 'html') ? '<br>'   : "\n";
    # Heading
    $tags{h2start} = ($format eq 'html') ? '<b>'   : '';
    $tags{h2end}   = ($format eq 'html') ? '</b>'  : '';
    # Bold font
    $tags{bstart}  = ($format eq 'html') ? '<b>'    : '';
    $tags{bend}    = ($format eq 'html') ? '</b>'   : '';
    # Verbatim
    $tags{vstart}  = ($format eq 'html') ? '<pre>'  : '';
    $tags{vend}    = ($format eq 'html') ? '</pre>' : '';
    return %tags;
}

sub slic3r_info
{
    my (%params) = @_;
    my %tag = Slic3r::tags($params{format});
    my $out = '';
    $out .= "$tag{bstart}$Slic3r::FORK_NAME$tag{bend}$tag{eol}";
    $out .= "$tag{bstart}Version: $tag{bend}$Slic3r::VERSION$tag{eol}";
    $out .= "$tag{bstart}Build:   $tag{bend}$Slic3r::BUILD$tag{eol}";
    return $out;
}

sub copyright_info
{
    my (%params) = @_;
    my %tag = Slic3r::tags($params{format});
    my $out =
        'Copyright &copy; 2016 Vojtech Bubnik, Prusa Research. <br />' .
        'Copyright &copy; 2011-2016 Alessandro Ranellucci. <br />' .
        '<a href="http://slic3r.org/">Slic3r</a> is licensed under the ' .
        '<a href="http://www.gnu.org/licenses/agpl-3.0.html">GNU Affero General Public License, version 3</a>.' .
        '<br /><br /><br />' .
        'Contributions by Henrik Brix Andersen, Nicolas Dandrimont, Mark Hindess, Petr Ledvina, Y. Sapir, Mike Sheldrake and numerous others. ' .
        'Manual by Gary Hodgson. Inspired by the RepRap community. <br />' .
        'Slic3r logo designed by Corey Daniels, <a href="http://www.famfamfam.com/lab/icons/silk/">Silk Icon Set</a> designed by Mark James. ';
    return $out;
}

sub system_info
{
    my (%params) = @_;
    my %tag = Slic3r::tags($params{format});

    my $out = '';
    $out .= "$tag{bstart}Operating System:    $tag{bend}$Config{osname}$tag{eol}";
    $out .= "$tag{bstart}System Architecture: $tag{bend}$Config{archname}$tag{eol}";        
    if ($^O eq 'MSWin32') {
        $out .= "$tag{bstart}Windows Version: $tag{bend}" . `ver` . $tag{eol};
    } else {
        # Hopefully some kind of unix / linux.
        $out .= "$tag{bstart}System Version: $tag{bend}" . `uname -a` . $tag{eol};
    }
    $out .= $tag{vstart} . Config::myconfig . $tag{vend};
    $out .= "  $tag{bstart}\@INC:$tag{bend}$tag{eol}$tag{vstart}";
    foreach my $i (@INC) {
        $out .= "    $i\n";
    }
    $out .= "$tag{vend}";
    return $out;
}

# this package declaration prevents an ugly fatal warning to be emitted when
# spawning a new thread
package GLUquadricObjPtr;

1;
