# This package loads all the non-GUI Slic3r perl packages.

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

BEGIN {
    $debug = 1 if (defined($ENV{'SLIC3R_DEBUGOUT'}) && $ENV{'SLIC3R_DEBUGOUT'} == 1);
    print "Debugging output enabled\n" if $debug;
}

use FindBin;

# Let the XS module know where the GUI resources reside.
set_resources_dir(decode_path($FindBin::Bin) . (($^O eq 'darwin') ? '/../Resources' : '/resources'));
set_var_dir(resources_dir() . "/icons");
set_local_dir(resources_dir() . "/localization/");

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
use Slic3r::Print::Object;
use Slic3r::Surface;
our $build = eval "use Slic3r::Build; 1";

# Scaling between the float and integer coordinates.
# Floats are in mm.
use constant SCALING_FACTOR         => 0.000001;

# Set the logging level at the Slic3r XS module.
$Slic3r::loglevel = (defined($ENV{'SLIC3R_LOGLEVEL'}) && $ENV{'SLIC3R_LOGLEVEL'} =~ /^[1-9]/) ? $ENV{'SLIC3R_LOGLEVEL'} : 0;
set_logging_level($Slic3r::loglevel);

# Let the palceholder parser evaluate one expression to initialize its local static macro_processor 
# class instance in a thread safe manner.
Slic3r::GCode::PlaceholderParser->new->evaluate_boolean_expression('1==1');

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

1;
