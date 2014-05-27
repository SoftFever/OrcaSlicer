use Test::More tests => 3;
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
    $config->set('skirts', 1);
    $config->set('skirt_height', 2);
    $config->set('perimeters', 0);
    $config->set('support_material_speed', 99);
    $config->set('cooling', 0);                     # to prevent speeds to be altered
    $config->set('first_layer_speed', '100%');      # to prevent speeds to be altered
    
    my $test = sub {
        my ($conf) = @_;
        $conf ||= $config;
        
        my $print = Slic3r::Test::init_print(['20mm_cube','20mm_cube'], config => $config);
        
        my %layers_with_skirt = ();  # Z => $count
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if (defined $self->Z) {
                $layers_with_skirt{$self->Z} //= 0;
                $layers_with_skirt{$self->Z} = 1
                    if $info->{extruding} && ($args->{F} // $self->F) == $config->support_material_speed*60;
            }
        });
        fail "wrong number of layers with skirt"
            unless (grep $_, values %layers_with_skirt) == $config->skirt_height;
    };
    
    ok $test->(), "skirt_height is honored when printing multiple objects too";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 0);
    $config->set('top_solid_layers', 0);            # to prevent solid shells and their speeds
    $config->set('bottom_solid_layers', 0);         # to prevent solid shells and their speeds
    $config->set('brim_width', 5);
    $config->set('support_material_speed', 99);
    $config->set('cooling', 0);                     # to prevent speeds to be altered
    $config->set('first_layer_speed', '100%');      # to prevent speeds to be altered
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    my %layers_with_brim = ();  # Z => $count
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if (defined $self->Z) {
            $layers_with_brim{$self->Z} //= 0;
            $layers_with_brim{$self->Z} = 1
                if $info->{extruding} && $info->{dist_XY} > 0 && ($args->{F} // $self->F) != $config->infill_speed*60;
        }
    });
    is scalar(grep $_, values %layers_with_brim), 1, "brim is generated";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 1);
    $config->set('brim_width', 10);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'successful G-code generation when skirt is smaller than brim width';
}

__END__
