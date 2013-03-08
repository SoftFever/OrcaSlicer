use Test::More tests => 2;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 0);
    
    my $test = sub {
        my ($conf) = @_;
        $conf ||= $config;
        
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        
        my %layers_with_shells = ();  # Z => $count
        Slic3r::Test::GCodeReader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($self->Z > 0) {
                $layers_with_shells{$self->Z} //= 0;
                $layers_with_shells{$self->Z} = 1 if $info->{extruding} && $info->{dist_XY} > 0;
            }
        });
        my @shells = @layers_with_shells{sort { $a <=> $b } keys %layers_with_shells};
        fail "wrong number of bottom solid layers"
            unless !defined(first { !$_ } @shells[0..$config->bottom_solid_layers-1]);
        fail "wrong number of top solid layers"
            unless !defined(first { !$_ } @shells[-$config->top_solid_layers..-1]);
        1;
    };
    
    ok $test->(), "proper number of shells is applied";
    $config->set('fill_density', 0);
    
    ok $test->(), "proper number of shells is applied even when fill density is none";
}

__END__
