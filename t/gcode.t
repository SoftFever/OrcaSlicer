use Test::More tests => 23;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(scale convex_hull);
use Slic3r::Test;

{
    my $gcodegen = Slic3r::GCode->new(
        layer_count     => 1,
        extruders       => [],
    );
    $gcodegen->set_origin(Slic3r::Pointf->new(10, 10));
    is_deeply $gcodegen->last_pos->arrayref, [scale -10, scale -10], 'last_pos is shifted correctly';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('wipe', [1]);
    $config->set('retract_layer_change', [0]);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my $have_wipe = 0;
    my @retract_speeds = ();
    my $extruded_on_this_layer = 0;
    my $wiping_on_new_layer = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{travel} && $info->{dist_Z}) {
            # changing layer
            $extruded_on_this_layer = 0;
        } elsif ($info->{extruding} && $info->{dist_XY}) {
            $extruded_on_this_layer = 1;
        } elsif ($info->{retracting} && $info->{dist_XY} > 0) {
            $have_wipe = 1;
            $wiping_on_new_layer = 1 if !$extruded_on_this_layer;
            my $move_time = $info->{dist_XY} / ($args->{F} // $self->F);
            push @retract_speeds, abs($info->{dist_E}) / $move_time;
        }
    });
    
    ok $have_wipe, "wipe";
    ok !defined (first { abs($_ - $config->retract_speed->[0]*60) < 5 } @retract_speeds), 'wipe moves don\'t retract faster than configured speed';
    ok !$wiping_on_new_layer, 'no wiping after layer change';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('z_offset', 5);
    $config->set('start_gcode', '');
    
    my $test = sub {
        my ($comment) = @_;
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $moves_below_z_offset = 0;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($info->{travel} && exists $args->{Z}) {
                $moves_below_z_offset++ if $args->{Z} < $config->z_offset;
            }
        });
        is $moves_below_z_offset, 0, "no Z moves below Z offset ($comment)";
    };
    
    $test->("no lift");
    
    $config->set('retract_lift', [3]);
    $test->("lift < z_offset");
    
    $config->set('retract_lift', [6]);
    $test->("lift > z_offset");
}

{
    # This tests the following behavior:
    # - complete objects does not crash
    # - no hard-coded "E" are generated
    # - Z moves are correctly generated for both objects
    # - no travel moves go outside skirt
    # - temperatures are set correctly
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('gcode_comments', 1);
    $config->set('complete_objects', 1);
    $config->set('extrusion_axis', 'A');
    $config->set('start_gcode', '');  # prevent any default extra Z move
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', 0.4);
    $config->set('temperature', [200]);
    $config->set('first_layer_temperature', [210]);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config, duplicate => 2);
    ok my $gcode = Slic3r::Test::gcode($print), "complete_objects";
    my @z_moves = ();
    my @travel_moves = ();  # array of scaled points
    my @extrusions = ();    # array of scaled points
    my @temps = ();
    Slic3r::GCode::Reader->new->parse($gcode, sub {
        my ($self, $cmd, $args, $info) = @_;
        fail 'unexpected E argument' if defined $args->{E};
        if (defined $args->{Z}) {
            push @z_moves, $args->{Z};
        }
        
        if ($info->{dist_XY}) {
            if ($info->{extruding} || $args->{A}) {
                push @extrusions, Slic3r::Point->new_scale($info->{new_X}, $info->{new_Y});
            } else {
                push @travel_moves, Slic3r::Point->new_scale($info->{new_X}, $info->{new_Y})
                    if @extrusions;  # skip initial travel move to first skirt point
            }
        } elsif ($cmd eq 'M104' || $cmd eq 'M109') {
            push @temps, $args->{S} if !@temps || $args->{S} != $temps[-1];
        }
    });
    my $layer_count = 20/0.4;  # cube is 20mm tall
    is scalar(@z_moves), 2*$layer_count, 'complete_objects generates the correct number of Z moves';
    is_deeply [ @z_moves[0..($layer_count-1)] ], [ @z_moves[$layer_count..$#z_moves] ], 'complete_objects generates the correct Z moves';
    
    my $convex_hull = convex_hull(\@extrusions);
    ok !(defined first { !$convex_hull->contains_point($_) } @travel_moves), 'all travel moves happen within skirt';

    is_deeply \@temps, [210, 200, 210, 200, 0], 'expected temperature changes';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('retract_length', [1000000]);
    $config->set('use_relative_e_distances', 1);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    Slic3r::Test::gcode($print);
    ok $print->print->total_used_filament > 0, 'final retraction is not considered in total used filament';
}

{
    my $test = sub {
        my ($print, $comment) = @_;
        
        my @percent = ();
        my $got_100 = 0;
        my $extruding_after_100 = 0;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
        
            if ($cmd eq 'M73') {
                push @percent, $args->{P};
                $got_100 = 1 if $args->{P} eq '100';
            }
            if ($info->{extruding} && $got_100) {
                $extruding_after_100 = 1;
            }
        });
        # the extruder heater is turned off when M73 P100 is reached
        ok !(defined first { $_ > 100 } @percent), "M73 is never given more than 100% ($comment)";
        ok !$extruding_after_100, "no extrusions after M73 P100 ($comment)";
    };
    
    {
        my $config = Slic3r::Config->new_from_defaults;
        $config->set('gcode_flavor', 'sailfish');
        $config->set('raft_layers', 3);
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        $test->($print, 'single object');
    }
    
    {
        my $config = Slic3r::Config->new_from_defaults;
        $config->set('gcode_flavor', 'sailfish');
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config, duplicate => 2);
        $test->($print, 'two copies of single object');
    }
    
    {
        my $config = Slic3r::Config->new_from_defaults;
        $config->set('gcode_flavor', 'sailfish');
        my $print = Slic3r::Test::init_print(['20mm_cube','20mm_cube'], config => $config);
        $test->($print, 'two objects');
    }
    
    {
        my $config = Slic3r::Config->new_from_defaults;
        $config->set('gcode_flavor', 'sailfish');
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config, scale_xyz => [1,1, 1/(20/$config->layer_height) ]);
        $test->($print, 'one layer object');
    }
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('start_gcode', 'START:[input_filename]');
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my $gcode = Slic3r::Test::gcode($print);
    like $gcode, qr/START:20mm_cube/, '[input_filename] is also available in custom G-code';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('spiral_vase', 1);
    my $print = Slic3r::Test::init_print('cube_with_hole', config => $config);
    
    my $spiral = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && exists $args->{E} && exists $args->{Z}) {
            $spiral = 1;
        }
    });
    
    ok !$spiral, 'spiral vase is correctly disabled on layers with multiple loops';
}

__END__
