use Test::More tests => 4;
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
        config      => Slic3r::Config->new_from_defaults,
        layer_count => 1,
        extruders   => [],
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
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('complete_objects', 1);
    $config->set('duplicate', 2);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), "complete_objects";
}

__END__
