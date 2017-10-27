use Test::More;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Surface ':types';
use Slic3r::Test;

plan tests => 8;

{
    my $test = sub {
        my ($config) = @_;
        
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        ok my $gcode = Slic3r::Test::gcode($print), "infill_every_layers does not crash";
        
        my $tool = undef;
        my %layers = ();        # layer_z => 1
        my %layer_infill = ();  # layer_z => has_infill
        Slic3r::GCode::Reader->new->parse($gcode, sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($cmd =~ /^T(\d+)/) {
                $tool = $1;
            } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0 && $tool != $config->support_material_extruder-1) {
                $layer_infill{$self->Z} //= 0;
                if ($tool == $config->infill_extruder-1) {
                    $layer_infill{$self->Z} = 1;
                }
            }
            $layers{$args->{Z}} = 1 if $cmd eq 'G1' && $info->{dist_Z} > 0;
        });
        
        my $layers_with_perimeters = scalar(keys %layer_infill);
        my $layers_with_infill = grep $_ > 0,  values %layer_infill;
        is scalar(keys %layers), $layers_with_perimeters+$config->raft_layers, 'expected number of layers';
        
        # first infill layer is never combined, so we don't consider it
        $layers_with_infill--;
        $layers_with_perimeters--;
        
        # we expect that infill is generated for half the number of combined layers
        # plus for each single layer that was not combined (remainder)
        is $layers_with_infill,
            int($layers_with_perimeters/$config->infill_every_layers) + ($layers_with_perimeters % $config->infill_every_layers),
            'infill is only present in correct number of layers';
    };
    
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('layer_height', 0.2);
    $config->set('first_layer_height', 0.2);
    $config->set('nozzle_diameter', [0.5]);
    $config->set('infill_every_layers', 2);
    $config->set('perimeter_extruder', 1);
    $config->set('infill_extruder', 2);
    $config->set('support_material_extruder', 3);
    $config->set('support_material_interface_extruder', 3);
    $config->set('top_solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    $test->($config);
    
    $config->set('skirts', 0);  # prevent usage of perimeter_extruder in raft layers
    $config->set('raft_layers', 5);
    $test->($config);
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('layer_height', 0.2);
    $config->set('first_layer_height', 0.2);
    $config->set('nozzle_diameter', [0.5]);
    $config->set('infill_every_layers', 2);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    $print->process;
    
    ok defined(first { @{$_->get_region(0)->fill_surfaces->filter_by_type(S_TYPE_INTERNALVOID)} > 0 }
        @{$print->print->get_object(0)->layers}),
        'infill combination produces internal void surfaces';
    
    # we disable combination after infill has been generated
    $config->set('infill_every_layers', 1);
    $print->apply_config($config);
    $print->process;
    
    ok !(defined first { @{$_->get_region(0)->fill_surfaces} == 0 }
        @{$print->print->get_object(0)->layers}),
            'infill combination is idempotent';
}

# the following needs to be adapted to the new API
if (0) {
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('skirts', 0);
    $config->set('solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    $config->set('top_solid_layers', 0);
    $config->set('infill_every_layers', 6);
    $config->set('layer_height', 0.06);
    $config->set('perimeters', 1);
    
    my $test = sub {
        my ($shift) = @_;
        
        my $self = Slic3r::Test::init_print('20mm_cube', config => $config);
        
        $shift /= &Slic3r::SCALING_FACTOR;
        my $scale = 4; # make room for fat infill lines with low layer height

        # Put a slope on the box's sides by shifting x and y coords by $tilt * (z / boxheight).
        # The test here is to put such a slight slope on the walls that it should
        # not trigger any extra fill on fill layers that should be empty when 
        # combine infill is enabled.
        $_->[0] += $shift * ($_->[2] / (20 / &Slic3r::SCALING_FACTOR)) for @{$self->objects->[0]->meshes->[0]->vertices};
        $_->[1] += $shift * ($_->[2] / (20 / &Slic3r::SCALING_FACTOR)) for @{$self->objects->[0]->meshes->[0]->vertices};
        $_ = [$_->[0]*$scale, $_->[1]*$scale, $_->[2]] for @{$self->objects->[0]->meshes->[0]->vertices};
                
        # copy of Print::export_gcode() up to the point 
        # after fill surfaces are combined
        $_->slice for @{$self->objects};
        $_->make_perimeters for @{$self->objects};
        $_->detect_surfaces_type for @{$self->objects};
        $_->prepare_fill_surfaces for map @{$_->regions}, map @{$_->layers}, @{$self->objects};
        $_->process_external_surfaces for map @{$_->regions}, map @{$_->layers}, @{$self->objects};
        $_->discover_horizontal_shells for @{$self->objects};
        $_->combine_infill for @{$self->objects};

        # Only layers with id % 6 == 0 should have fill.
        my $spurious_infill = 0;
        foreach my $layer (map @{$_->layers}, @{$self->objects}) {
            ++$spurious_infill if ($layer->id % 6 && grep @{$_->fill_surfaces} > 0, @{$layer->regions});
        }

        $spurious_infill -= scalar(@{$self->objects->[0]->layers} - 1) % 6;
        
        fail "spurious fill surfaces found on layers that should have none (walls " . sprintf("%.4f", Slic3r::Geometry::rad2deg(atan2($shift, 20/&Slic3r::SCALING_FACTOR))) . " degrees off vertical)"
            unless $spurious_infill == 0;
        1;
    };
    
    # Test with mm skew offsets for the top of the 20mm-high box
    for my $shift (0, 0.0001, 1) {
        ok $test->($shift), "no spurious fill surfaces with box walls " . sprintf("%.4f",Slic3r::Geometry::rad2deg(atan2($shift, 20))) . " degrees off of vertical";
    }
}

__END__
