use Test::More tests => 4;
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
    $config->set('solid_infill_speed', 99);
    $config->set('top_solid_infill_speed', 99);
    $config->set('first_layer_speed', '100%');
    $config->set('cooling', 0);
    
    my $test = sub {
        my ($conf) = @_;
        $conf ||= $config;
        
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        
        my %layers_with_shells = ();  # Z => $count
        Slic3r::GCode::Reader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($self->Z > 0) {
                $layers_with_shells{$self->Z} //= 0;
                $layers_with_shells{$self->Z} = 1
                    if $info->{extruding}
                        && $info->{dist_XY} > 0
                        && ($args->{F} // $self->F) == $config->solid_infill_speed*60;
            }
        });
        my @shells = @layers_with_shells{sort { $a <=> $b } keys %layers_with_shells};
        fail "insufficient number of bottom solid layers"
            unless !defined(first { !$_ } @shells[0..$config->bottom_solid_layers-1]);
        fail "excessive number of bottom solid layers"
            unless scalar(grep $_, @shells[0 .. $#shells/2]) == $config->bottom_solid_layers;
        fail "insufficient number of top solid layers"
            unless !defined(first { !$_ } @shells[-$config->top_solid_layers..-1]);
        fail "excessive number of top solid layers"
            unless scalar(grep $_, @shells[($#shells/2)..$#shells]) == $config->top_solid_layers;
        1;
    };
    
    ok $test->(), "proper number of shells is applied";
    
    $config->set('top_solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    ok $test->(), "no shells are applied when both top and bottom are set to zero";
    
    $config->set('fill_density', 0);
    ok $test->(), "proper number of shells is applied even when fill density is none";
}

# issue #1161
{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('layer_height', 0.3);
    $config->set('first_layer_height', '100%');
    $config->set('bottom_solid_layers', 0);
    $config->set('top_solid_layers', 3);
    $config->set('cooling', 0);
    $config->set('solid_infill_speed', 99);
    $config->set('top_solid_infill_speed', 99);
    $config->set('first_layer_speed', '100%');
    
    my $print = Slic3r::Test::init_print('V', config => $config);
    my %layers_with_solid_infill = ();  # Z => 1
    Slic3r::GCode::Reader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
        my ($self, $cmd, $args, $info) = @_;
        
        $layers_with_solid_infill{$self->Z} = 1
            if $info->{extruding} && ($args->{F} // $self->F) == $config->solid_infill_speed*60;
    });
    is scalar(map $layers_with_solid_infill{$_}, grep $_ <= 7.2, keys %layers_with_solid_infill), 3,
        "correct number of top solid shells is generated in V-shaped object";
}

__END__
