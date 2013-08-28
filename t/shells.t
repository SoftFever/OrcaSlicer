use Test::More tests => 11;
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
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
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
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        $layers_with_solid_infill{$self->Z} = 1
            if $info->{extruding} && ($args->{F} // $self->F) == $config->solid_infill_speed*60;
    });
    is scalar(map $layers_with_solid_infill{$_}, grep $_ <= 7.2, keys %layers_with_solid_infill), 3,
        "correct number of top solid shells is generated in V-shaped object";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    # we need to check against one perimeter because this test is calibrated
    # (shape, extrusion_width) so that perimeters cover the bottom surfaces of
    # their lower layer - the test checks that shells are not generated on the
    # above layers (thus 'across' the shadow perimeter)
    # the test is actually calibrated to leave a narrow bottom region for each
    # layer - we test that in case of fill_density = 0 such narrow shells are 
    # discarded instead of grown
    $config->set('perimeters', 1);
    $config->set('fill_density', 0);
    $config->set('cooling', 0);                 # prevent speed alteration
    $config->set('first_layer_speed', '100%');  # prevent speed alteration
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', '100%');
    $config->set('extrusion_width', 0.55);
    $config->set('bottom_solid_layers', 3);
    $config->set('top_solid_layers', 0);
    $config->set('solid_infill_speed', 99);
    
    my $print = Slic3r::Test::init_print('V', config => $config);
    my %layers = ();  # Z => 1
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        $layers{$self->Z} = 1
            if $info->{extruding} && ($args->{F} // $self->F) == $config->solid_infill_speed*60;
    });
    is scalar(keys %layers), $config->bottom_solid_layers,
        "shells are not propagated across perimeters of the neighbor layer";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('spiral_vase', 1);
    $config->set('bottom_solid_layers', 0);
    $config->set('skirts', 0);
    $config->set('first_layer_height', '100%');
    
    # TODO: this needs to be tested with a model with sloping edges, where starting
    # points of each layer are not aligned - in that case we would test that no
    # travel moves are left to move to the new starting point - in a cube, end
    # points coincide with next layer starting points (provided there's no clipping)
    my $test = sub {
        my ($model_name, $description) = @_;
        my $print = Slic3r::Test::init_print($model_name, config => $config);
        my $travel_moves_after_first_extrusion = 0;
        my $started_extruding = 0;
        my @z_steps = ();
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            $started_extruding = 1 if $info->{extruding};
            push @z_steps, ($args->{Z} - $self->Z)
                if $started_extruding && exists $args->{Z};
            $travel_moves_after_first_extrusion++
                if $info->{travel} && $started_extruding && !exists $args->{Z};
        });
        is $travel_moves_after_first_extrusion, 0, "no gaps in spiral vase ($description)";
        ok !(grep { $_ > $config->layer_height } @z_steps), "no gaps in Z ($description)";
    };
    
    $test->('20mm_cube', 'solid model');
    $test->('40x10', 'hollow model');
    
    $config->set('z_offset', -10);
    $test->('20mm_cube', 'solid model with negative z-offset');
}

__END__
