use Test::More tests => 8;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(scale);
use Slic3r::Test;

{
    my $gcodegen = Slic3r::GCode->new(
        layer_count     => 1,
        extruders       => [],
    );
    $gcodegen->set_shift(10, 10);
    is_deeply $gcodegen->last_pos->arrayref, [scale -10, scale -10], 'last_pos is shifted correctly';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('wipe', [1]);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my $have_wipe = 0;
    my @retract_speeds = ();
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        if ($info->{retracting} && $info->{dist_XY} > 0) {
            $have_wipe = 1;
            my $move_time = $info->{dist_XY} / ($args->{F} // $self->F);
            push @retract_speeds, abs($info->{dist_E}) / $move_time;
        }
    });
    
    ok $have_wipe, "wipe";
    ok !defined (first { abs($_ - $config->retract_speed->[0]*60) < 5 } @retract_speeds), 'wipe moves don\'t retract faster than configured speed';
}

{
    # This tests the following behavior:
    # - complete objects does not crash
    # - no hard-coded "E" are generated
    # - Z moves are correctly generated for both objects
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('gcode_comments', 1);
    $config->set('complete_objects', 1);
    $config->set('extrusion_axis', 'A');
    $config->set('start_gcode', '');  # prevent any default extra Z move
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', 0.4);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config, duplicate => 2);
    ok my $gcode = Slic3r::Test::gcode($print), "complete_objects";
    my @z_moves = ();
    Slic3r::GCode::Reader->new->parse($gcode, sub {
        my ($self, $cmd, $args, $info) = @_;
        fail 'unexpected E argument' if defined $args->{E};
        if (defined $args->{Z}) {
            push @z_moves, $args->{Z};
        }
    });
    my $layer_count = 20/0.4;  # cube is 20mm tall
    is scalar(@z_moves), 2*$layer_count, 'complete_objects generates the correct number of Z moves';
    is_deeply [ @z_moves[0..($layer_count-1)] ], [ @z_moves[$layer_count..$#z_moves] ], 'complete_objects generates the correct Z moves';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('retract_length', [1000000]);
    $config->set('use_relative_e_distances', 1);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        
    });
    ok $print->print->total_used_filament > 0, 'final retraction is not considered in total used filament';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('gcode_flavor', 'sailfish');
    $config->set('raft_layers', 3);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my @percent = ();
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'M73') {
            push @percent, $args->{P};
        }
    });
    # the extruder heater is turned off when M73 P100 is reached
    ok !(defined first { $_ > 100 } @percent), 'M73 is never given more than 100%';
}

__END__
