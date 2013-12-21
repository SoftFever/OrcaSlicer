#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 32;

{
    my $config = Slic3r::Config->new;
    
    $config->set('layer_height', 0.3);
    ok abs($config->get('layer_height') - 0.3) < 1e-4, 'set/get float';
    is $config->serialize('layer_height'), '0.3', 'serialize float';
    
    $config->set('perimeters', 2);
    is $config->get('perimeters'), 2, 'set/get int';
    is $config->serialize('perimeters'), '2', 'serialize int';
    
    $config->set('extrusion_axis', 'A');
    is $config->get('extrusion_axis'), 'A', 'set/get string';
    is $config->serialize('extrusion_axis'), 'A', 'serialize string';
    
    $config->set('notes', "foo\nbar");
    is $config->get('notes'), "foo\nbar", 'set/get string with newline';
    is $config->serialize('notes'), 'foo\nbar', 'serialize string with newline';
    $config->set_deserialize('notes', 'bar\nbaz');
    is $config->get('notes'), "bar\nbaz", 'deserialize string with newline';
    
    $config->set('first_layer_height', 0.3);
    ok abs($config->get('first_layer_height') - 0.3) < 1e-4, 'set/get absolute floatOrPercent';
    is $config->serialize('first_layer_height'), '0.3', 'serialize absolute floatOrPercent';
    
    $config->set('first_layer_height', '50%');
    ok abs($config->get_abs_value('first_layer_height') - 0.15) < 1e-4, 'set/get relative floatOrPercent';
    is $config->serialize('first_layer_height'), '50%', 'serialize relative floatOrPercent';
    
    $config->set('print_center', [50,80]);
    is_deeply $config->get('print_center'), [50,80], 'set/get point';
    is $config->serialize('print_center'), '50,80', 'serialize point';
    $config->set_deserialize('print_center', '20,10');
    is_deeply $config->get('print_center'), [20,10], 'deserialize point';
    
    $config->set('use_relative_e_distances', 1);
    is $config->get('use_relative_e_distances'), 1, 'set/get bool';
    is $config->serialize('use_relative_e_distances'), '1', 'serialize bool';
    
    $config->set('gcode_flavor', 'teacup');
    is $config->get('gcode_flavor'), 'teacup', 'set/get enum';
    is $config->serialize('gcode_flavor'), 'teacup', 'serialize enum';
    
    $config->set('extruder_offset', [[10,20],[30,45]]);
    is_deeply $config->get('extruder_offset'), [[10,20],[30,45]], 'set/get points';
    is $config->serialize('extruder_offset'), '10x20,30x45', 'serialize points';
    $config->set_deserialize('extruder_offset', '20x10');
    is_deeply $config->get('extruder_offset'), [[20,10]], 'deserialize points';
    
    # truncate ->get() to first decimal digit
    $config->set('nozzle_diameter', [0.2,0.3]);
    is_deeply [ map int($_*10)/10, @{$config->get('nozzle_diameter')} ], [0.2,0.3], 'set/get floats';
    is $config->serialize('nozzle_diameter'), '0.2,0.3', 'serialize floats';
    $config->set_deserialize('nozzle_diameter', '0.1,0.4');
    is_deeply [ map int($_*10)/10, @{$config->get('nozzle_diameter')} ], [0.1,0.4], 'deserialize floats';
    
    $config->set('temperature', [180,210]);
    is_deeply $config->get('temperature'), [180,210], 'set/get ints';
    is $config->serialize('temperature'), '180,210', 'serialize ints';
    $config->set_deserialize('temperature', '195,220');
    is_deeply $config->get('temperature'), [195,220], 'deserialize ints';
    
    $config->set('wipe', [1,0]);
    is_deeply $config->get('wipe'), [1,0], 'set/get bools';
    is $config->serialize('wipe'), '1,0', 'serialize bools';
    $config->set_deserialize('wipe', '0,1,1');
    is_deeply $config->get('wipe'), [0,1,1], 'deserialize bools';
}

__END__
