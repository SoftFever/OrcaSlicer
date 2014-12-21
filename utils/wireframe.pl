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
use Slic3r;
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scale unscale X Y PI);

my %opt = (
    step_height         => 5,
    nozzle_angle        => 30,
    nozzle_width        => 10,
    first_layer_height  => 0.3,
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
    $opt{output_file} or usage(1);
    $ARGV[0] or usage(1);
}

{
    # load model
    my $model = Slic3r::Model->read_from_file($ARGV[0]);
    $model->add_default_instances;
    $model->center_instances_around_point(Slic3r::Pointf->new(100,100));
    my $mesh = $model->mesh;
    $mesh->translate(0, 0, -$mesh->bounding_box->z_min);
    
    # get slices
    my @z = ();
    my $z_max = $mesh->bounding_box->z_max;
    for (my $z = $opt{first_layer_height}; $z <= $z_max; $z += $opt{step_height}) {
        push @z, $z;
    }
    my @slices = @{$mesh->slice(\@z)};
    
    my $flow = Slic3r::Flow->new(
        width           => 0.35,
        height          => 0.35,
        nozzle_diameter => 0.35,
        bridge          => 1,
    );
    
    my $config = Slic3r::Config::Print->new;
    $config->set('gcode_comments', 1);
    
    open my $fh, '>', $opt{output_file};
    my $gcodegen = Slic3r::GCode->new(
        enable_loop_clipping => 0,  # better bonding
    );
    $gcodegen->apply_print_config($config);
    $gcodegen->set_extruders([0]);
    print $fh $gcodegen->set_extruder(0);
    print $fh $gcodegen->writer->preamble;
    
    my $e = $gcodegen->writer->extruder->e_per_mm3 * $flow->mm3_per_mm;
    
    foreach my $layer_id (0..$#z) {
        my $z = $z[$layer_id];
        
        foreach my $island (@{$slices[$layer_id]}) {
            foreach my $polygon (@$island) {
                if ($layer_id > 0) {
                    # find the lower polygon that we want to connect to this one
                    my $lower = $slices[$layer_id-1]->[0]->contour;  # 't was easy, wasn't it?
                    my $lower_z = $z[$layer_id-1];
                    
                    {
                        my @points = ();
                        
                        # keep all points with strong angles
                        {
                            my @pp = @$polygon;
                            foreach my $i (0..$#pp) {
                                push @points, $pp[$i-1] if abs($pp[$i-1]->ccw_angle($pp[$i-2], $pp[$i]) - PI) > PI/3;
                            }
                        }
                        
                        $polygon = Slic3r::Polygon->new(@points);
                    }
                    #$polygon = Slic3r::Polygon->new(@{$polygon->split_at_first_point->equally_spaced_points(scale $opt{nozzle_width})});
                    
                    # find vertical lines
                    my @vertical = ();
                    foreach my $point (@{$polygon}) {
                        push @vertical, Slic3r::Line->new($point->projection_onto_polygon($lower), $point);
                    }
                    
                    next if !@vertical;
                
                    my @points = ();
                    foreach my $line (@vertical) {
                        push @points, Slic3r::Pointf3->new(
                            unscale($line->a->x),
                            unscale($line->a->y),  #))
                            $lower_z,
                        );
                        push @points, Slic3r::Pointf3->new(
                            unscale($line->b->x),
                            unscale($line->b->y),  #))
                            $z,
                        );
                    }
                
                    # reappend first point as destination of the last diagonal segment
                    push @points, Slic3r::Pointf3->new(
                        unscale($vertical[0]->a->x),
                        unscale($vertical[0]->a->y),  #))
                        $lower_z,
                    );
                
                    # move to the position of the first vertical line
                    print $fh $gcodegen->writer->travel_to_xyz(shift @points);
                
                    # extrude segments
                    foreach my $point (@points) {
                        print $fh $gcodegen->writer->extrude_to_xyz($point, $e * $gcodegen->writer->get_position->distance_to($point));
                    }
                }
            }
            
        print $fh $gcodegen->writer->travel_to_z($z);
            foreach my $polygon (@$island) {
                #my $polyline = $polygon->split_at_vertex(Slic3r::Point->new_scale(@{$gcodegen->writer->get_position}[0,1]));
                my $polyline = $polygon->split_at_first_point;
                print $fh $gcodegen->writer->travel_to_xy(Slic3r::Pointf->new_unscale(@{ $polyline->first_point }), "move to first contour point");
                
                foreach my $line (@{$polyline->lines}) {
                    my $point = Slic3r::Pointf->new_unscale(@{ $line->b });
                    print $fh $gcodegen->writer->extrude_to_xy($point, $e * unscale($line->length));
                }
            }
        }
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
