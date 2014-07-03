#!/usr/bin/perl
# This script displays 3D preview of a mesh

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Getopt::Long qw(:config no_auto_abbrev);
use Slic3r;
use Slic3r::GUI;
use Slic3r::GUI::PreviewCanvas;
$|++;

my %opt = ();
{
    my %options = (
        'help'                  => sub { usage() },
        'load=s'                => \$opt{load},
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

{
    # load model
    my $model = Slic3r::Model->read_from_file($ARGV[0]);
    
    # load config
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    if ($opt{load}) {
        $config->apply(Slic3r::Config->load($opt{load}));
    }
    
    # init print
    my $sprint = Slic3r::Print::Simple->new;
    $sprint->apply_config($config);
    $sprint->set_model($model);
    $sprint->process;
    
    # visualize toolpaths
    $Slic3r::ViewToolpaths::print = $sprint->_print;
    my $app = Slic3r::ViewToolpaths->new;
    $app->MainLoop;
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: view-toolpaths.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    --load CONFIG       Loads the supplied config file
    
EOF
    exit ($exit_code || 0);
}


package Slic3r::ViewToolpaths;
use Wx qw(:sizer);
use base qw(Wx::App);

our $print;

sub OnInit {
    my $self = shift;
    
    my $frame = Wx::Frame->new(undef, -1, 'Toolpaths', [-1, -1], [500, 500]);
    my $panel = Wx::Panel->new($frame, -1);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add(Slic3r::GUI::Plater::2DToolpaths->new($panel, $print), 1, wxEXPAND, 0);
    $panel->SetSizer($sizer);
    
    $frame->Show(1);
}

__END__
