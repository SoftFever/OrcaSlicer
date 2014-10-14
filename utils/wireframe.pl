#!/usr/bin/perl
# This script exports experimental G-code for wireframe printing
# (inspired by the brilliant WirePrint concept)

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Getopt::Long qw(:config no_auto_abbrev);
use PDF::API2;
use Slic3r;
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scale unscale X Y);

my %opt = (
    step_height => 10,
);
{
    my %options = (
        'help'                  => sub { usage() },
        'output|o=s'            => \$opt{output_file},
        'step-height|h=f'       => \$opt{step_height},
        'nozzle-angle|a=f'      => \$opt{nozzle_angle},
    );
    GetOptions(%options) or usage(1);
    $opt{output_file} or usage(1);
    ### Input file is not needed until we use hard-coded geometry:
    ### $ARGV[0] or usage(1);
}

{
    my $flow = Slic3r::Flow->new(
        width           => 0.35,
        height          => 0.35,
        nozzle_diameter => 0.35,
        bridge          => 1,
    );
    
    my $section;
    
    # build a square section
    {
        my $dist = 2 * $opt{step_height};  # horizontal step
        my $side_modules = 3;
        my @points = (
            [0,0],
            (map [$_*$dist, 0], 1..$side_modules),
            (map [$side_modules*$dist, $_*$dist], 1..$side_modules),
            (map [($_-1)*$dist, $side_modules*$dist], reverse 1..$side_modules),
            (map [0, ($_-1)*$dist], reverse 1..$side_modules),
        );
        pop @points;  # prevent coinciding endpoints
        $section = Slic3r::Polygon->new_scale(@points);
    }
    my $section_loop = Slic3r::ExtrusionLoop->new_from_paths(
        Slic3r::ExtrusionPath->new(
            polyline        => $section->split_at_first_point,
            role            => EXTR_ROLE_BRIDGE,
            mm3_per_mm      => $flow->mm3_per_mm,
            width           => $flow->width,
            height          => $flow->height,
        )
    );
    
    my $vertical_steps = 3;
    
    open my $fh, '>', $opt{output_file};
    my $gcodegen = Slic3r::GCode->new(
        layer_count => $vertical_steps,
    );
    {
        my $print_config = Slic3r::Config::Print->new;
        $gcodegen->set_extruders([0], $print_config);
        $gcodegen->set_extruder(0);
    }
    
    print $fh "G21 ; set units to millimeters\n";
    print $fh "G90 ; use absolute coordinates\n";
    
    # print one section on layer 0
    $gcodegen->extrude_loop($section_loop);
    
    close $fh;
}

sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: wireframe.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    --output, -o        Write to the specified file
    --step-height, -h   Use the specified step height
    --nozzle-angle, -a  Max nozzle angle
    
EOF
    exit ($exit_code || 0);
}

__END__
