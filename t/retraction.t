use Test::More;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(epsilon);
use Slic3r::Test;

{
    my $config = Slic3r::Config->new(
        retract_length          => [1.5],
        retract_before_travel   => [3],
    );
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    my $retracted = 1;  # ignore the first travel move from home to first point
    Slic3r::Test::GCodeReader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{retracting}) {
            ok Slic3r::Test::compare(-$info->{dist_E}, $config->retract_length->[0]),
                'retracted by the right amount';
            $retracted = 1;
        }
        if ($info->{extruding}) {
            $retracted = 0;
        }
        if ($info->{travel} && $info->{dist_XY} >= $config->retract_before_travel->[0]) {
            ok $retracted,
                'retracted before long travel move';
        }
    });
}

done_testing;

__END__
