use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw();
use Slic3r;
use Slic3r::Geometry qw();
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('pressure_advance', 10);
    $config->set('retract_length', [1]);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config, duplicate => 2);
    my $retracted = 0;
    my $extruding_before_full_unretract = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{extruding} && $info->{dist_XY}) {
            $extruding_before_full_unretract if $retracted != 0;
        } elsif ($info->{extruding}) {
            $retracted += -$info->{dist_E};
        } elsif ($info->{retracting}) {
            $retracted += -$info->{dist_E};
        }
    });
    
    ok !$extruding_before_full_unretract, 'not extruding before complete unretract';
}


__END__
