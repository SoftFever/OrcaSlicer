use Test::More tests => 5;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(PI);
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('fill_density', 0);
    $config->set('perimeters', 3);
    $config->set('top_solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    $config->set('cooling', 0);                     # to prevent speeds from being altered
    $config->set('first_layer_speed', '100%');      # to prevent speeds from being altered
    
    {
        my $print = Slic3r::Test::init_print('overhang', config => $config);
        my $has_cw_loops = 0;
        my $cur_loop;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $cur_loop ||= [ [$self->X, $self->Y] ];
                push @$cur_loop, [ @$info{qw(new_X new_Y)} ];
            } else {
                if ($cur_loop) {
                    $has_cw_loops = 1 if Slic3r::Polygon->new(@$cur_loop)->is_clockwise;
                    $cur_loop = undef;
                }
            }
        });
        ok !$has_cw_loops, 'all perimeters extruded ccw';
    }
    
    {
        $config->set('external_perimeter_speed', 68);
        my $print = Slic3r::Test::init_print('cube_with_hole', config => $config);
        my $has_cw_loops = my $has_outwards_move = 0;
        my $cur_loop;
        my %external_loops = ();  # print_z => count of external loops
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $cur_loop ||= [ [$self->X, $self->Y] ];
                push @$cur_loop, [ @$info{qw(new_X new_Y)} ];
            } else {
                if ($cur_loop) {
                    $has_cw_loops = 1 if Slic3r::Polygon->new_scale(@$cur_loop)->is_clockwise;
                    if ($self->F == $config->external_perimeter_speed*60) {
                        my $move_dest = Slic3r::Point->new_scale(@$info{qw(new_X new_Y)});
                        $external_loops{$self->Z}++;
                        $has_outwards_move = 1
                            if !Slic3r::Polygon->new_scale(@$cur_loop)->encloses_point($move_dest)
                                ? ($external_loops{$self->Z} == 2)  # contour should include destination
                                : ($external_loops{$self->Z} == 1); # hole should not
                    }
                    $cur_loop = undef;
                }
            }
        });
        ok !$has_cw_loops, 'all perimeters extruded ccw';
        ok !$has_outwards_move, 'move inwards after completing external loop';
    }
    
    {
        $config->set('start_perimeters_at_concave_points', 1);
        my $print = Slic3r::Test::init_print('L', config => $config);
        my $loop_starts_from_convex_point = 0;
        my $cur_loop;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $cur_loop ||= [ [$self->X, $self->Y] ];
                push @$cur_loop, [ @$info{qw(new_X new_Y)} ];
            } else {
                if ($cur_loop) {
                    $loop_starts_from_convex_point = 1
                        if Slic3r::Geometry::angle3points(@$cur_loop[0,-1,1]) >= PI;
                    $cur_loop = undef;
                }
            }
        });
        ok !$loop_starts_from_convex_point, 'avoid starting from convex points';
    }
    
    {
        $config->set('perimeters', 1);
        $config->set('perimeter_speed', 77);
        $config->set('external_perimeter_speed', 66);
        $config->set('bridge_speed', 99);
        $config->set('cooling', 1);
        $config->set('fan_below_layer_time', 0);
        $config->set('slowdown_below_layer_time', 0);
        $config->set('bridge_fan_speed', 100);
        $config->set('overhangs', 1);
        my $print = Slic3r::Test::init_print('overhang', config => $config);
        my %layer_speeds = ();  # print Z => [ speeds ]
        my $fan_speed = 0;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            $fan_speed = 0 if $cmd eq 'M107';
            $fan_speed = $args->{S} if $cmd eq 'M106';
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $layer_speeds{$self->Z} ||= {};
                $layer_speeds{$self->Z}{my $feedrate = $args->{F} // $self->F} = 1;
                
                fail 'wrong speed found'
                    if $feedrate != $config->perimeter_speed*60
                        && $feedrate != $config->external_perimeter_speed*60
                        && $feedrate != $config->bridge_speed*60;
                
                if ($feedrate == $config->bridge_speed*60) {
                    fail 'printing overhang but fan is not enabled or running at wrong speed'
                        if $fan_speed != 255;
                } else {
                    fail 'fan is running when not supposed to'
                        if $fan_speed > 0;
                }
            }
        });
        is scalar(grep { keys %$_ > 1 } values %layer_speeds), 1,
            'only overhang layer has more than one speed';
    }
}

__END__
