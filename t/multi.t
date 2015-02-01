use Test::More tests => 13;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(scale convex_hull);
use Slic3r::Geometry::Clipper qw(offset);
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('raft_layers', 2);
    $config->set('infill_extruder', 2);
    $config->set('solid_infill_extruder', 3);
    $config->set('support_material_extruder', 4);
    $config->set('ooze_prevention', 1);
    $config->set('extruder_offset', [ [0,0], [20,0], [0,20], [20,20] ]);
    $config->set('temperature', [200, 180, 170, 160]);
    $config->set('first_layer_temperature', [206, 186, 166, 156]);
    $config->set('toolchange_gcode', ';toolchange');  # test that it doesn't crash when this is supplied
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    my $tool = undef;
    my @tool_temp = (0,0,0,0);
    my @toolchange_points = ();
    my @extrusion_points = ();
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd =~ /^T(\d+)/) {
            # ignore initial toolchange
            if (defined $tool) {
                my $expected_temp = $self->Z == ($config->get_value('first_layer_height') + $config->z_offset)
                    ? $config->first_layer_temperature->[$tool]
                    : $config->temperature->[$tool];
                die 'standby temperature was not set before toolchange'
                    if $tool_temp[$tool] != $expected_temp + $config->standby_temperature_delta;
                
                push @toolchange_points, my $point = Slic3r::Point->new_scale($self->X, $self->Y);
            }
            $tool = $1;
        } elsif ($cmd eq 'M104' || $cmd eq 'M109') {
            my $t = $args->{T} // $tool;
            if ($tool_temp[$t] == 0) {
                fail 'initial temperature is not equal to first layer temperature + standby delta'
                    unless $args->{S} == $config->first_layer_temperature->[$t] + $config->standby_temperature_delta;
            }
            $tool_temp[$t] = $args->{S};
        } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            push @extrusion_points, my $point = Slic3r::Point->new_scale($args->{X}, $args->{Y});
            $point->translate(map +scale($_), @{ $config->extruder_offset->[$tool] });
        }
    });
    my $convex_hull = convex_hull(\@extrusion_points);
    
    my @t = ();
    foreach my $point (@toolchange_points) {
        foreach my $offset (@{$config->extruder_offset}) {
            push @t, my $p = $point->clone;
            $p->translate(map +scale($_), @$offset);
        }
    }
    ok !(defined first { $convex_hull->contains_point($_) } @t), 'all nozzles are outside skirt at toolchange';
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(
            "ooze_prevention_test.svg",
            no_arrows   => 1,
            polygons    => [$convex_hull],
            red_points  => \@t,
            points      => \@toolchange_points,
        );
    }
    
    # offset the skirt by the maximum displacement between extruders plus a safety extra margin
    my $delta = scale(20 * sqrt(2) + 1);
    my $outer_convex_hull = offset([$convex_hull], +$delta)->[0];
    ok !(defined first { !$outer_convex_hull->contains_point($_) } @toolchange_points), 'all toolchanges happen within expected area';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('support_material_extruder', 3);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'no errors when using non-consecutive extruders';
}

{
    my $config = Slic3r::Config->new;
    $config->set('extruder', 2);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    like Slic3r::Test::gcode($print), qr/ T1/, 'extruder shortcut';
}

{
    my $config = Slic3r::Config->new;
    $config->set('perimeter_extruder', 2);
    $config->set('infill_extruder', 2);
    $config->set('support_material_extruder', 2);
    $config->set('support_material_interface_extruder', 2);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'no errors when using multiple skirts with a single, non-zero, extruder';
}

{
    my $model = stacked_cubes();
    my $lower_config = $model->get_material('lower')->config;
    my $upper_config = $model->get_material('upper')->config;
    
    $lower_config->set('extruder', 1);
    $lower_config->set('bottom_solid_layers', 0);
    $lower_config->set('top_solid_layers', 1);
    $upper_config->set('extruder', 2);
    $upper_config->set('bottom_solid_layers', 1);
    $upper_config->set('top_solid_layers', 0);
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('fill_density', 0);
    $config->set('solid_infill_speed', 99);
    $config->set('top_solid_infill_speed', 99);
    $config->set('cooling', 0);                 # for preventing speeds from being altered
    $config->set('first_layer_speed', '100%');  # for preventing speeds from being altered
    
    my $test = sub {
        my $print = Slic3r::Test::init_print($model, config => $config);
        my $tool = undef;
        my %T0_shells = my %T1_shells = ();  # Z => 1
        Slic3r::GCode::Reader->new->parse(my $gcode = Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
        
            if ($cmd =~ /^T(\d+)/) {
                $tool = $1;
            } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
                if (($args->{F} // $self->F) == $config->solid_infill_speed*60) {
                    if ($tool == 0) {
                        $T0_shells{$self->Z} = 1;
                    } elsif ($tool == 1) {
                        $T1_shells{$self->Z} = 1;
                    }
                }
            }
        });
        return [ sort keys %T0_shells ], [ sort keys %T1_shells ];
    };
    
    {
        my ($t0, $t1) = $test->();
        is scalar(@$t0), 0, 'no interface shells';
        is scalar(@$t1), 0, 'no interface shells';
    }
    {
        $config->set('interface_shells', 1);
        my ($t0, $t1) = $test->();
        is scalar(@$t0), $lower_config->top_solid_layers,    'top interface shells';
        is scalar(@$t1), $upper_config->bottom_solid_layers, 'bottom interface shells';
    }
}

{
    my $model = stacked_cubes();
    my $object = $model->objects->[0];
    
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', '100%');
    $config->set('skirts', 0);
    my $print = Slic3r::Test::init_print($model, config => $config);
    
    is $object->volumes->[0]->config->extruder, 1, 'auto_assign_extruders() assigned correct extruder to first volume';
    is $object->volumes->[1]->config->extruder, 2, 'auto_assign_extruders() assigned correct extruder to second volume';
    
    my $tool = undef;
    my %T0 = my %T1 = ();  # Z => 1
    Slic3r::GCode::Reader->new->parse(my $gcode = Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
    
        if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
        } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            if ($tool == 0) {
                $T0{$self->Z} = 1;
            } elsif ($tool == 1) {
                $T1{$self->Z} = 1;
            }
        }
    });
    
    ok !(defined first { $_ > 20 } keys %T0), 'T0 is never used for upper object';
    ok !(defined first { $_ < 20 } keys %T1), 'T1 is never used for lower object';
}

sub stacked_cubes {
    my $model = Slic3r::Model->new;
    my $object = $model->add_object;
    $object->add_volume(mesh => Slic3r::Test::mesh('20mm_cube'), material_id => 'lower');
    $object->add_volume(mesh => Slic3r::Test::mesh('20mm_cube', translate => [0,0,20]), material_id => 'upper');
    $object->add_instance(offset => Slic3r::Pointf->new(0,0));
    
    return $model;
}

__END__
