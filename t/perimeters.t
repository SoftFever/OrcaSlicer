use Test::More tests => 2;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('fill_density', 0);
    $config->set('perimeters', 3);
    $config->set('top_solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    $config->set('cooling', 0);                     # to prevent speeds to be altered
    $config->set('first_layer_speed', '100%');      # to prevent speeds to be altered
    
    {
        my $print = Slic3r::Test::init_print('overhang', config => $config);
        my $has_cw_loops = 0;
        my $cur_loop;
        Slic3r::GCode::Reader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($info->{extruding}) {
                $cur_loop ||= [ [$self->X, $self->Y] ];
                push @$cur_loop, [ @$info{qw(new_X new_Y)} ];
            } else {
                if ($cur_loop) {
                    $has_cw_loops = 1 if !Slic3r::Geometry::Clipper::is_counter_clockwise($cur_loop);
                    $cur_loop = undef;
                }
            }
        });
        ok !$has_cw_loops, 'all perimeters extruded ccw';
    }
    
    {
        $config->set('perimeters', 1);
        $config->set('perimeter_speed', 77);
        $config->set('external_perimeter_speed', 66);
        $config->set('bridge_speed', 99);
        my $print = Slic3r::Test::init_print('overhang', config => $config);
        my %layer_speeds = ();  # print Z => [ speeds ]
        Slic3r::GCode::Reader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $layer_speeds{$self->Z} ||= {};
                $layer_speeds{$self->Z}{my $feedrate = $args->{F} // $self->F} = 1;
                fail 'wrong speed found'
                    if $feedrate != $config->perimeter_speed*60
                        && $feedrate != $config->external_perimeter_speed*60
                        && $feedrate != $config->bridge_speed*60;
            }
        });
        is scalar(grep { keys %$_ > 1 } values %layer_speeds), 1,
            'only overhang layer has more than one speed';
    }
}

__END__
