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
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

{
    my $model = Slic3r::Model->read_from_file($ARGV[0]);
    
    $Slic3r::ViewMesh::object = $model->objects->[0];
    my $app = Slic3r::ViewMesh->new;
    $app->MainLoop;
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: view-mesh.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    
EOF
    exit ($exit_code || 0);
}

package Slic3r::ViewMesh;
use Wx qw(:sizer);
use base qw(Wx::App);

our $object;

sub OnInit {
    my $self = shift;
    
    my $frame = Wx::Frame->new(undef, -1, 'Mesh Viewer', [-1, -1], [500, 400]);
    my $panel = Wx::Panel->new($frame, -1);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add(Slic3r::GUI::PreviewCanvas->new($panel, $object), 1, wxEXPAND, 0);
    $panel->SetSizer($sizer);
    $sizer->SetSizeHints($panel);
    
    $frame->Show(1);
}

__END__
