#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 16;

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
}

__END__
