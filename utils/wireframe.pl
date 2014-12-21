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
    first_layer_height => 0.3,
);
{
    my %options = (
        'help'                  => sub { usage() },
        'output|o=s'            => \$opt{output_file},
        'step-height|h=f'       => \$opt{step_height},
        'nozzle-angle|a=f'      => \$opt{nozzle_angle},
        'nozzle-width|w=f'      => \$opt{nozzle_width},
        'first-layer-height=f'  => \$opt{first_layer_height},
    );
    GetOptions(%options) or usage(1);
    $opt{output_file} or usage(1);ì
    $ARGV[0] or usage(1);
}

{
    # load model
    my $model = Slic3r::Model->read_from_file($ARGV[0]);
    my $mesh = $model->mesh;
    $mesh->translate(0, 0, -$mesh->bounding_box->z_min);
    
    # get slices
    my @z = ();
    my $z_max = $mesh->bounding_box->z_max;
    for (my $z = $opt{first_layer_height}; $z <= $z_max; $z += $opt{step_height}) {
        push @z, $z;
    }
    
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
        enable_loop_clipping => 0,  # better bonding
    );
    $gcodegen->set_extruders([0]);
    print $fh $gcodegen->set_extruder(0);
    print $fh $gcodegen->writer->preamble;
    
    foreach my $z (map $_*$opt{step_height}, 0..($vertical_steps-1)) {
        print $fh $gcodegen->writer->travel_to_z($z + $flow->height);
        print $fh $gcodegen->extrude_loop($section_loop, "contour");
    }
    
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
    --nozzle-width, -w  External nozzle diameter
    
EOF
    exit ($exit_code || 0);
}

__END__
