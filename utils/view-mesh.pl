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
        'cut=f'                 => \$opt{cut},
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

{
    my $model = Slic3r::Model->read_from_file($ARGV[0]);
    
    # make sure all objects have at least one defined instance
    $model->add_default_instances;
    
    my $app = Slic3r::ViewMesh->new;
    $app->{canvas}->load_object($model->objects->[0]);
    $app->{canvas}->set_bounding_box($model->objects->[0]->bounding_box);
    $app->{canvas}->set_auto_bed_shape;
    $app->{canvas}->SetCuttingPlane($opt{cut}) if defined $opt{cut};
    $app->MainLoop;
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: view-mesh.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    --cut Z             Display the cutting plane at the given Z
    
EOF
    exit ($exit_code || 0);
}

package Slic3r::ViewMesh;
use Wx qw(:sizer);
use base qw(Wx::App);

sub OnInit {
    my $self = shift;
    
    my $frame = Wx::Frame->new(undef, -1, 'Mesh Viewer', [-1, -1], [500, 400]);
    my $panel = Wx::Panel->new($frame, -1);
    
    $self->{canvas} = Slic3r::GUI::PreviewCanvas->new($panel);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($self->{canvas}, 1, wxEXPAND, 0);
    $panel->SetSizer($sizer);
    $sizer->SetSizeHints($panel);
    
    $frame->Show(1);
}

__END__
