use Test::More tests => 13;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(epsilon);
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    my @contact_z = my @top_z = ();
    
    my $test = sub {
        my $flow = Slic3r::Flow->new(nozzle_diameter => $config->nozzle_diameter->[0], layer_height => $config->layer_height);
        my @support_layers = Slic3r::Print::Object::_compute_support_layers(\@contact_z, \@top_z, $config, $flow);
        
        is $support_layers[0], $config->first_layer_height,
            'first layer height is honored';
        is scalar(grep { $support_layers[$_]-$support_layers[$_-1] <= 0 } 1..$#support_layers), 0,
            'no null or negative support layers';
        is scalar(grep { $support_layers[$_]-$support_layers[$_-1] > $flow->nozzle_diameter + epsilon } 1..$#support_layers), 0,
            'no layers thicker than nozzle diameter';
        
        my $wrong_top_spacing = 0;
        foreach my $top_z (@top_z) {
            # find layer index of this top surface
            my $layer_id = first { abs($support_layers[$_] - $top_z) < epsilon } 0..$#support_layers;
            
            # check that first support layer above this top surface is spaced with nozzle diameter
            $wrong_top_spacing = 1
                if ($support_layers[$layer_id+1] - $support_layers[$layer_id]) != $flow->nozzle_diameter;
        }
        ok !$wrong_top_spacing, 'layers above top surfaces are spaced correctly';
    };
    
    $config->set('layer_height', 0.2);
    $config->set('first_layer_height', 0.3);
    @contact_z = (1.9);
    @top_z = (1.1);
    $test->();
    
    $config->set('first_layer_height', 0.4);
    $test->();
    
    $config->set('layer_height', $config->nozzle_diameter->[0]);
    $test->();
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('raft_layers', 3);
    $config->set('brim_width',  6);
    $config->set('skirts', 0);
    $config->set('support_material_extruder', 2);
    $config->set('support_material_interface_extruder', 2);
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', '100%');
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok my $gcode = Slic3r::Test::gcode($print), 'no conflict between raft/support and brim';
    
    my $tool = 0;
    Slic3r::GCode::Reader->new->parse($gcode, sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
        } elsif ($info->{extruding} && $self->Z <= ($config->raft_layers * $config->layer_height)) {
            fail 'not extruding raft/brim with support material extruder'
                if $tool != ($config->support_material_extruder-1);
        }
    });
}

__END__
