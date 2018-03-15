#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 147;

foreach my $config (Slic3r::Config->new, Slic3r::Config::Static::new_FullPrintConfig) {
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

    foreach my $test_data (
        {
            name => 'empty',
            values => [],
            serialized => ''
        },
        {
            name => 'single empty',
            values => [''],
            serialized => '""'
        },
        {
            name => 'single noempty, simple',
            values => ['RGB'],
            serialized => 'RGB'
        },
        {
            name => 'multiple noempty, simple',
            values => ['ABC', 'DEF', '09182745@!#$*(&'],
            serialized => 'ABC;DEF;09182745@!#$*(&'
        },
        {
            name => 'multiple, simple, some empty',
            values => ['ABC', 'DEF', '', '09182745@!#$*(&', ''],
            serialized => 'ABC;DEF;;09182745@!#$*(&;'
        },
        {
            name => 'complex',
            values => ['some "quoted" notes', "yet\n some notes", "whatever \n notes", ''],
            serialized => '"some \"quoted\" notes";"yet\n some notes";"whatever \n notes";'
        }
        )
    {    
        $config->set('filament_notes', $test_data->{values});
        is $config->serialize('filament_notes'), $test_data->{serialized}, 'serialize multi-string value ' . $test_data->{name};
        $config->set_deserialize('filament_notes', '');
        is_deeply $config->get('filament_notes'), [], 'deserialize multi-string value - empty ' . $test_data->{name};
        $config->set_deserialize('filament_notes', $test_data->{serialized});
        is_deeply $config->get('filament_notes'), $test_data->{values}, 'deserialize complex multi-string value ' . $test_data->{name};
    }

    $config->set('first_layer_height', 0.3);
    ok abs($config->get('first_layer_height') - 0.3) < 1e-4, 'set/get absolute floatOrPercent';
    is $config->serialize('first_layer_height'), '0.3', 'serialize absolute floatOrPercent';
    
    $config->set('first_layer_height', '50%');
    $config->get_abs_value('first_layer_height');
    ok abs($config->get_abs_value('first_layer_height') - 0.15) < 1e-4, 'set/get relative floatOrPercent';
    is $config->serialize('first_layer_height'), '50%', 'serialize relative floatOrPercent';
    
    # Uh-oh, we have no point option to test at the moment
    #ok $config->set('print_center', [50,80]), 'valid point coordinates';
    #is_deeply $config->get('print_center'), [50,80], 'set/get point';
    #is $config->serialize('print_center'), '50,80', 'serialize point';
    #$config->set_deserialize('print_center', '20,10');
    #is_deeply $config->get('print_center'), [20,10], 'deserialize point';
    #ok !$config->set('print_center', ['t',80]), 'invalid point X';
    #ok !$config->set('print_center', [50,'t']), 'invalid point Y';
    
    $config->set('use_relative_e_distances', 1);
    is $config->get('use_relative_e_distances'), 1, 'set/get bool';
    is $config->serialize('use_relative_e_distances'), '1', 'serialize bool';
    
    $config->set('gcode_flavor', 'teacup');
    is $config->get('gcode_flavor'), 'teacup', 'set/get enum';
    is $config->serialize('gcode_flavor'), 'teacup', 'serialize enum';
    $config->set_deserialize('gcode_flavor', 'mach3');
    is $config->get('gcode_flavor'), 'mach3', 'deserialize enum (gcode_flavor)';
    $config->set_deserialize('gcode_flavor', 'machinekit');
    is $config->get('gcode_flavor'), 'machinekit', 'deserialize enum (gcode_flavor)';
    
    $config->set_deserialize('fill_pattern', 'line');
    is $config->get('fill_pattern'), 'line', 'deserialize enum (fill_pattern)';
    
    $config->set_deserialize('support_material_pattern', 'rectilinear');
    is $config->get('support_material_pattern'), 'rectilinear', 'deserialize enum (support_material_pattern)';
    
    $config->set('extruder_offset', [[10,20],[30,45]]);
    is_deeply [ map $_->pp, @{$config->get('extruder_offset')} ], [[10,20],[30,45]], 'set/get points';
    $config->set('extruder_offset', [Slic3r::Pointf->new(10,20),Slic3r::Pointf->new(30,45)]);
    is_deeply [ map $_->pp, @{$config->get('extruder_offset')} ], [[10,20],[30,45]], 'set/get points';
    is $config->serialize('extruder_offset'), '10x20,30x45', 'serialize points';
    $config->set_deserialize('extruder_offset', '20x10');
    is_deeply [ map $_->pp, @{$config->get('extruder_offset')} ], [[20,10]], 'deserialize points';
    $config->set_deserialize('extruder_offset', '0x0');
    is_deeply [ map $_->pp, @{$config->get('extruder_offset')} ], [[0,0]], 'deserialize points';
    {
        my @values = ([10,20]);
        $values[2] = [10,20];  # implicitely extend array; this is not the same as explicitely assigning undef to second item
        ok !$config->set('extruder_offset', \@values), 'reject undef points';
    }
    
    # truncate ->get() to first decimal digit
    $config->set('nozzle_diameter', [0.2,3]);
    is_deeply [ map int($_*10)/10, @{$config->get('nozzle_diameter')} ], [0.2,3], 'set/get floats';
    is $config->serialize('nozzle_diameter'), '0.2,3', 'serialize floats';
    $config->set_deserialize('nozzle_diameter', '0.1,0.4');
    is_deeply [ map int($_*10)/10, @{$config->get('nozzle_diameter')} ], [0.1,0.4], 'deserialize floats';
    $config->set_deserialize('nozzle_diameter', '3');
    is_deeply [ map int($_*10)/10, @{$config->get('nozzle_diameter')} ], [3], 'deserialize a single float';
    {
        my @values = (0.4);
        $values[2] = 2;  # implicitely extend array; this is not the same as explicitely assigning undef to second item
        ok !$config->set('nozzle_diameter', \@values), 'reject undef floats';
    }
    
    $config->set('temperature', [180,210]);
    is_deeply $config->get('temperature'), [180,210], 'set/get ints';
    is $config->serialize('temperature'), '180,210', 'serialize ints';
    $config->set_deserialize('temperature', '195,220');
    is_deeply $config->get('temperature'), [195,220], 'deserialize ints';
    {
        my @values = (180);
        $values[2] = 200;  # implicitely extend array; this is not the same as explicitely assigning undef to second item
        ok !$config->set('temperature', \@values), 'reject undef ints';
    }
    
    $config->set('wipe', [1,0]);
    is_deeply $config->get('wipe'), [1,0], 'set/get bools';
    is $config->get_at('wipe', 0), 1, 'get_at bools';
    is $config->get_at('wipe', 1), 0, 'get_at bools';
    is $config->get_at('wipe', 9), 1, 'get_at bools';
    is $config->serialize('wipe'), '1,0', 'serialize bools';
    $config->set_deserialize('wipe', '0,1,1');
    is_deeply $config->get('wipe'), [0,1,1], 'deserialize bools';
    $config->set_deserialize('wipe', '');
    is_deeply $config->get('wipe'), [], 'deserialize bools from empty string';
    $config->set_deserialize('retract_layer_change', 0);
    is_deeply $config->get('retract_layer_change'), [0], 'deserialize bools from non-string value';
    {
        my @values = (1);
        $values[2] = 1;  # implicitely extend array; this is not the same as explicitely assigning undef to second item
        ok !$config->set('wipe', \@values), 'reject undef bools';
    }
    
    $config->set('post_process', ['foo','bar']);
    is_deeply $config->get('post_process'), ['foo','bar'], 'set/get strings';
    is $config->serialize('post_process'), 'foo;bar', 'serialize strings';
    $config->set_deserialize('post_process', 'bar;baz');
    is_deeply $config->get('post_process'), ['bar','baz'], 'deserialize strings';
    {
        my @values = ('foo');
        $values[2] = 'bar';  # implicitely extend array; this is not the same as explicitely assigning undef to second item
        ok !$config->set('post_process', \@values), 'reject undef strings';
    }
    
    is_deeply [ sort @{$config->get_keys} ], [ sort keys %{$config->as_hash} ], 'get_keys and as_hash';
}

{
    my $config = Slic3r::Config->new;
    $config->set('perimeters', 2);
    
    # test that no crash happens when using set_deserialize() with a key that hasn't been set() yet
    $config->set_deserialize('filament_diameter', '3');
    
    my $config2 = Slic3r::Config::Static::new_FullPrintConfig;
    $config2->apply_dynamic($config);
    is $config2->get('perimeters'), 2, 'apply_dynamic';
}

{
    my $config = Slic3r::Config::Static::new_FullPrintConfig;
    my $config2 = Slic3r::Config->new;
    $config2->apply_static($config);
    is $config2->get('perimeters'), Slic3r::Config::print_config_def()->{perimeters}{default}, 'apply_static and print_config_def';
    
    $config->set('top_solid_infill_speed', 70);
    is $config->get_abs_value('top_solid_infill_speed'), 70, 'get_abs_value() works when ratio_over references a floatOrPercent option';
}

{
    my $config = Slic3r::Config->new;
    $config->set('fill_pattern', 'line');

    my $config2 = Slic3r::Config->new;
    $config2->set('fill_pattern', 'hilbertcurve');
    
    is $config->get('fill_pattern'), 'line', 'no interferences between DynamicConfig objects';
}

{
    my $config = Slic3r::Config->new;
    # the pair [0,0] is part of the test, since it checks whether the 0x0 serialized value is correctly parsed
    $config->set('extruder_offset', [ [0,0], [20,0], [0,20] ]);
    my $config2 = Slic3r::Config->new;
    $config2->apply($config);
    is_deeply [ map $_->pp, @{$config->get('extruder_offset')} ], [ map $_->pp, @{$config2->get('extruder_offset')} ],
        'apply dynamic over dynamic';
}

{
    my $config = Slic3r::Config->new;
    $config->set('extruder', 2);
    $config->set('perimeter_extruder', 3);
    $config->normalize;
    ok !$config->has('extruder'), 'extruder option is removed after normalize()';
    is $config->get('infill_extruder'), 2, 'undefined extruder is populated with default extruder';
    is $config->get('perimeter_extruder'), 3, 'defined extruder is not overwritten by default extruder';
}

{
    my $config = Slic3r::Config->new;
    $config->set('infill_extruder', 2);
    $config->normalize;
    is $config->get('solid_infill_extruder'), 2, 'undefined solid infill extruder is populated with infill extruder';
}

{
    my $config = Slic3r::Config->new;
    $config->set('spiral_vase', 1);
    $config->set('retract_layer_change', [1,0]);
    $config->normalize;
    is_deeply $config->get('retract_layer_change'), [0,0], 'retract_layer_change is disabled with spiral_vase';
}

{
    use Cwd qw(abs_path);
    use File::Basename qw(dirname);
    my $path = abs_path($0);
    my $config = Slic3r::Config::load(dirname($path)."/inc/22_config_bad_config_options.ini");
    ok 1, 'did not crash on reading invalid items in config';
}

__END__
