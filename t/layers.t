use Test::More tests => 5;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Test qw(_eq);

my $config = Slic3r::Config->new_from_defaults;

my $test = sub {
    my ($conf) = @_;
    $conf ||= $config;
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $conf);
    
    my @z = ();
    my @increments = ();
    Slic3r::Test::GCodeReader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{dist_Z}) {
            push @z, 1*$args->{Z};
            push @increments, $info->{dist_Z};
        }
    });
    
    fail 'wrong first layer height'
        if $z[0] ne $config->get_value('first_layer_height') + $config->z_offset;
    
    fail 'wrong second layer height'
        if $z[1] ne $config->get_value('first_layer_height') + $config->get_value('layer_height') + $config->z_offset;
    
    fail 'wrong layer height'
        if first { !_eq($_, $config->layer_height) } @increments[1..$#increments];
    
    1;
};

$config->set('start_gcode',             '');  # to avoid dealing with the nozzle lift in start G-code
$config->set('layer_height', 0.3);
$config->set('first_layer_height', 0.2);
ok $test->(), "absolute first layer height";

$config->set('first_layer_height', '60%');
ok $test->(), "relative first layer height";

$config->set('z_offset', 0.9);
ok $test->(), "positive Z offset";

$config->set('z_offset', -0.8);
ok $test->(), "negative Z offset";

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('nozzle_diameter', [0.35]);
    $config->set('layer_height', 0.1333);
    
    my $print = Slic3r::Test::init_print('2x20x10', config => $config);
    $print->init_extruders;
    $_->region(0) for @{$print->objects->[0]->layers};  # init layer regions
    ok $print->objects->[0]->layers->[1]->support_material_contact_height > 0, 'support_material_contact_height is positive';
}

__END__
