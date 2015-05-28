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
use Slic3r::GUI::3DScene;
$|++;

my %opt = ();
{
    my %options = (
        'help'                  => sub { usage() },
        'load=s'                => \$opt{load},
        '3D'                    => \$opt{d3},
        'duplicate=i'           => \$opt{duplicate},
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

{
    # load model
    my $model = Slic3r::Model->read_from_file($ARGV[0]);
    
    # load config
    my $config = Slic3r::Config->new_from_defaults;
    if ($opt{load}) {
        $config->apply(Slic3r::Config->load($opt{load}));
    }
    
    # init print
    my $sprint = Slic3r::Print::Simple->new;
    $sprint->duplicate($opt{duplicate} // 1);
    $sprint->apply_config($config);
    $sprint->set_model($model);
    $sprint->process;
    
    # visualize toolpaths
    $Slic3r::ViewToolpaths::print = $sprint->_print;
    $Slic3r::ViewToolpaths::d3 = $opt{d3};
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
use base qw(Wx::App Class::Accessor);

our $print;
our $d3;

sub OnInit {
    my $self = shift;
    
    my $frame = Wx::Frame->new(undef, -1, 'Toolpaths', [-1, -1], [500, 500]);
    my $panel = Wx::Panel->new($frame, -1);
    
    my $canvas;
    if ($d3) {
        $canvas = Slic3r::GUI::3DScene->new($panel);
        $canvas->set_bed_shape($print->config->bed_shape);
        $canvas->load_print_toolpaths($print);
        
        foreach my $object (@{$print->objects}) {
            #$canvas->load_print_object_slices($object);
            $canvas->load_print_object_toolpaths($object);
            #$canvas->load_object($object->model_object);
        }
        $canvas->zoom_to_volumes;
    } else {
        $canvas = Slic3r::GUI::Plater::2DToolpaths->new($panel, $print);
    }
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($canvas, 1, wxEXPAND, 0);
    $panel->SetSizer($sizer);
    
    $frame->Show(1);
}

__END__
