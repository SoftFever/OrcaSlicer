use Test::More tests => 77;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Test;

{
    my $config = Slic3r::Config::new_from_defaults;
    
    my $test = sub {
        my ($conf) = @_;
        $conf ||= $config;
        
        my $print = Slic3r::Test::init_print('2x20x10', config => $conf);
        
        my $last_move_was_z_change = 0;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($last_move_was_z_change && $cmd ne $config->layer_gcode) {
                fail 'custom layer G-code was not applied after Z change';
            }
            if (!$last_move_was_z_change && $cmd eq $config->layer_gcode) {
                fail 'custom layer G-code was not applied after Z change';
            }
            
            $last_move_was_z_change = (defined $info->{dist_Z} && $info->{dist_Z} > 0);
        });
        
        1;
    };
    
    $config->set('start_gcode', '_MY_CUSTOM_START_GCODE_');  # to avoid dealing with the nozzle lift in start G-code
    $config->set('layer_gcode', '_MY_CUSTOM_LAYER_GCODE_');
    ok $test->(), "custom layer G-code is applied after Z move and before other moves";
}

#==========================================================

{
    my $parser = Slic3r::GCode::PlaceholderParser->new;
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('printer_notes', '  PRINTER_VENDOR_PRUSA3D  PRINTER_MODEL_MK2  ');
    $config->set('nozzle_diameter', [0.6, 0.6, 0.6, 0.6]);
    $parser->apply_config($config);
    $parser->set('foo' => 0);
    $parser->set('bar' => 2);
    $parser->set('num_extruders' => 4);
    is $parser->process('[temperature_[foo]]'),
        $config->temperature->[0],
        "nested config options (legacy syntax)";
    is $parser->process('{temperature[foo]}'),
        $config->temperature->[0],
        "array reference";
    is $parser->process("test [ temperature_ [foo] ] \n hu"),
        "test " . $config->temperature->[0] . " \n hu",
        "whitespaces and newlines are maintained";
    is $parser->process('{2*3}'),     '6',   'math: 2*3';
    is $parser->process('{2*3/6}'),   '1',   'math: 2*3/6';
    is $parser->process('{2*3/12}'),  '0',   'math: 2*3/12';
    ok abs($parser->process('{2.*3/12}') - 0.5) < 1e-7, 'math: 2.*3/12';
    is $parser->process('{2*(3-12)}'), '-18', 'math: 2*(3-12)';
    is $parser->process('{2*foo*(3-12)}'), '0', 'math: 2*foo*(3-12)';
    is $parser->process('{2*bar*(3-12)}'), '-36', 'math: 2*bar*(3-12)';
    ok abs($parser->process('{2.5*bar*(3-12)}') - -45) < 1e-7, 'math: 2.5*bar*(3-12)';

    # Test the boolean expression parser.
    is $parser->evaluate_boolean_expression('12 == 12'), 1, 'boolean expression parser: 12 == 12';
    is $parser->evaluate_boolean_expression('12 != 12'), 0, 'boolean expression parser: 12 != 12';
    is $parser->evaluate_boolean_expression('"has some PATTERN embedded" =~ /.*PATTERN.*/'), 1, 'boolean expression parser: regex matches';
    is $parser->evaluate_boolean_expression('"has some PATTERN embedded" =~ /.*PTRN.*/'),    0, 'boolean expression parser: regex does not match';
    is $parser->evaluate_boolean_expression('foo + 2 == bar'),                               1, 'boolean expression parser: accessing variables, equal';
    is $parser->evaluate_boolean_expression('foo + 3 == bar'),                               0, 'boolean expression parser: accessing variables, not equal';

    is $parser->evaluate_boolean_expression('(12 == 12) and (13 != 14)'),                    1, 'boolean expression parser: (12 == 12) and (13 != 14)';
    is $parser->evaluate_boolean_expression('(12 == 12) && (13 != 14)'),                     1, 'boolean expression parser: (12 == 12) && (13 != 14)';
    is $parser->evaluate_boolean_expression('(12 == 12) or (13 == 14)'),                     1, 'boolean expression parser: (12 == 12) or (13 == 14)';
    is $parser->evaluate_boolean_expression('(12 == 12) || (13 == 14)'),                     1, 'boolean expression parser: (12 == 12) || (13 == 14)';
    is $parser->evaluate_boolean_expression('(12 == 12) and not (13 == 14)'),                1, 'boolean expression parser: (12 == 12) and not (13 == 14)';
    is $parser->evaluate_boolean_expression('(12 == 12) ? (1 - 1 == 0) : (2 * 2 == 3)'),     1, 'boolean expression parser: ternary true';
    is $parser->evaluate_boolean_expression('(12 == 21/2) ? (1 - 1 == 0) : (2 * 2 == 3)'),   0, 'boolean expression parser: ternary false';
    is $parser->evaluate_boolean_expression('(12 == 13) ? (1 - 1 == 3) : (2 * 2 == 4)'),     1, 'boolean expression parser: ternary false';
    is $parser->evaluate_boolean_expression('(12 == 2 * 6) ? (1 - 1 == 3) : (2 * 2 == 4)'),  0, 'boolean expression parser: ternary true';
    is $parser->evaluate_boolean_expression('12 < 3'), 0, 'boolean expression parser: lower than - false';
    is $parser->evaluate_boolean_expression('12 < 22'), 1, 'boolean expression parser: lower than - true';
    is $parser->evaluate_boolean_expression('12 > 3'), 1, 'boolean expression parser: greater than - true';
    is $parser->evaluate_boolean_expression('12 > 22'), 0, 'boolean expression parser: greater than - false';
    is $parser->evaluate_boolean_expression('12 <= 3'), 0, 'boolean expression parser: lower than or equal- false';
    is $parser->evaluate_boolean_expression('12 <= 22'), 1, 'boolean expression parser: lower than or equal - true';
    is $parser->evaluate_boolean_expression('12 >= 3'), 1, 'boolean expression parser: greater than or equal - true';
    is $parser->evaluate_boolean_expression('12 >= 22'), 0, 'boolean expression parser: greater than or equal - false';
    is $parser->evaluate_boolean_expression('12 <= 12'), 1, 'boolean expression parser: lower than or equal (same values) - true';
    is $parser->evaluate_boolean_expression('12 >= 12'), 1, 'boolean expression parser: greater than or equal (same values) - true';

    is $parser->evaluate_boolean_expression('printer_notes=~/.*PRINTER_VENDOR_PRUSA3D.*/ and printer_notes=~/.*PRINTER_MODEL_MK2.*/ and nozzle_diameter[0]==0.6 and num_extruders>1'), 1, 'complex expression';
    is $parser->evaluate_boolean_expression('printer_notes=~/.*PRINTER_VEwerfNDOR_PRUSA3D.*/ or printer_notes=~/.*PRINTertER_MODEL_MK2.*/ or (nozzle_diameter[0]==0.6 and num_extruders>1)'), 1, 'complex expression2';
    is $parser->evaluate_boolean_expression('printer_notes=~/.*PRINTER_VEwerfNDOR_PRUSA3D.*/ or printer_notes=~/.*PRINTertER_MODEL_MK2.*/ or (nozzle_diameter[0]==0.3 and num_extruders>1)'), 0, 'complex expression3';
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('output_filename_format', 'ts_[travel_speed]_lh_[layer_height].gcode');
    $config->set('start_gcode', "TRAVEL:[travel_speed] HEIGHT:[layer_height]\n");
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    my $output_file = $print->print->output_filepath;
    my ($t, $h) = map $config->$_, qw(travel_speed layer_height);
    ok $output_file =~ /ts_${t}_/, 'print config options are replaced in output filename';
    ok $output_file =~ /lh_$h\./, 'region config options are replaced in output filename';
    
    my $gcode = Slic3r::Test::gcode($print);
    ok $gcode =~ /TRAVEL:$t/, 'print config options are replaced in custom G-code';
    ok $gcode =~ /HEIGHT:$h/, 'region config options are replaced in custom G-code';
}

{
    my $config = Slic3r::Config->new;
    $config->set('extruder', 2);
    $config->set('first_layer_temperature', [200,205]);
    
    {
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $gcode = Slic3r::Test::gcode($print);
        ok $gcode =~ /M104 S205 T1/, 'temperature set correctly for non-zero yet single extruder';
        ok $gcode !~ /M104 S\d+ T0/, 'unused extruder correctly ignored';
    }
    
    $config->set('infill_extruder', 1);
    {
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $gcode = Slic3r::Test::gcode($print);
        ok $gcode =~ /M104 S200 T0/, 'temperature set correctly for first extruder';
        ok $gcode =~ /M104 S205 T1/, 'temperature set correctly for second extruder';
    }
    
    my @start_gcode = (qq!
;__temp0:[first_layer_temperature_0]__
;__temp1:[first_layer_temperature_1]__
;__temp2:[first_layer_temperature_2]__
    !, qq!
;__temp0:{first_layer_temperature[0]}__
;__temp1:{first_layer_temperature[1]}__
;__temp2:{first_layer_temperature[2]}__
    !);
    my @syntax_description = (' (legacy syntax)', ' (new syntax)');
    for my $i (0, 1) {
        $config->set('start_gcode', $start_gcode[$i]);
        {
            my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
            my $gcode = Slic3r::Test::gcode($print);
            # we use the [infill_extruder] placeholder to make sure this test doesn't
            # catch a false positive caused by the unparsed start G-code option itself
            # being embedded in the G-code
            ok $gcode =~ /temp0:200/, 'temperature placeholder for first extruder correctly populated' . $syntax_description[$i];
            ok $gcode =~ /temp1:205/, 'temperature placeholder for second extruder correctly populated' . $syntax_description[$i];
            ok $gcode =~ /temp2:200/, 'temperature placeholder for unused extruder populated with first value' . $syntax_description[$i];
        }
    }

    $config->set('start_gcode', qq!
;substitution:{if infill_extruder==1}extruder1
         {elsif infill_extruder==2}extruder2
         {else}extruder3{endif}
    !);
    {
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $gcode = Slic3r::Test::gcode($print);
        ok $gcode =~ /substitution:extruder1/, 'if / else / endif - first block returned';
    }
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('before_layer_gcode', ';BEFORE [layer_num]');
    $config->set('layer_gcode', ';CHANGE [layer_num]');
    $config->set('support_material', 1);
    $config->set('layer_height', 0.2);
    my $print = Slic3r::Test::init_print('overhang', config => $config);
    my $gcode = Slic3r::Test::gcode($print);
    
    my @before = ();
    my @change = ();
    foreach my $line (split /\R+/, $gcode) {
        if ($line =~ /;BEFORE (\d+)/) {
            push @before, $1;
        } elsif ($line =~ /;CHANGE (\d+)/) {
            push @change, $1;
            fail 'inconsistent layer_num before and after layer change'
                if $1 != $before[-1];
        }
    }
    is_deeply \@before, \@change, 'layer_num is consistent before and after layer changes';
    ok !defined(first { $change[$_] != $change[$_-1]+1 } 1..$#change),
        'layer_num grows continously';  # i.e. no duplicates or regressions
}

{
    my $config = Slic3r::Config->new;
    $config->set('start_gcode', qq!
;substitution:{if infill_extruder==1}if block
         {elsif infill_extruder==2}elsif block 1
         {elsif infill_extruder==3}elsif block 2
         {elsif infill_extruder==4}elsif block 3
         {else}endif block{endif}
    !);
    my @returned = ('', 'if block', 'elsif block 1', 'elsif block 2', 'elsif block 3', 'endif block');
    for my $i (1,2,3,4,5) {
        $config->set('infill_extruder', $i);
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $gcode = Slic3r::Test::gcode($print);
        my $found_other = 0;
        for my $j (1,2,3,4,5) {
            next if $i == $j;
            $found_other = 1 if $gcode =~ /substitution:$returned[$j]/;
        }
        ok $gcode =~ /substitution:$returned[$i]/, 'if / else / endif - ' . $returned[$i] . ' returned';
        ok !$found_other, 'if / else / endif - only ' . $returned[$i] . ' returned';
    }
}

{
    my $config = Slic3r::Config->new;
    $config->set('start_gcode', 
        ';substitution:{if infill_extruder==1}{if perimeter_extruder==1}block11{else}block12{endif}' .
        '{elsif infill_extruder==2}{if perimeter_extruder==1}block21{else}block22{endif}' .
        '{else}{if perimeter_extruder==1}block31{else}block32{endif}{endif}:end');
    for my $i (1,2,3) {
        $config->set('infill_extruder', $i);
        for my $j (1,2) {
            $config->set('perimeter_extruder', $j);
            my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
            my $gcode = Slic3r::Test::gcode($print);
            ok $gcode =~ /substitution:block$i$j:end/, "two level if / else / endif - block$i$j returned";
        }
    }
}

{
    my $config = Slic3r::Config->new;
    $config->set('start_gcode', 
        ';substitution:{if notes=="MK2"}MK2{elsif notes=="MK3"}MK3{else}MK1{endif}:end');
    for my $printer_name ("MK2", "MK3", "MK1") {
        $config->set('notes', $printer_name);
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $gcode = Slic3r::Test::gcode($print);
        ok $gcode =~ /substitution:$printer_name:end/, "printer name $printer_name matched";
    }
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('complete_objects', 1);
    $config->set('between_objects_gcode', '_MY_CUSTOM_GCODE_');
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config, duplicate => 3);
    my $gcode = Slic3r::Test::gcode($print);
    is scalar(() = $gcode =~ /^_MY_CUSTOM_GCODE_/gm), 2, 'between_objects_gcode is applied correctly';
}

__END__
