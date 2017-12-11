use Test::More tests => 6;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(unscale convex_hull);
use Slic3r::Test;

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('skirts', 1);
    $config->set('skirt_height', 2);
    $config->set('perimeters', 0);
    $config->set('support_material_speed', 99);
    $config->set('cooling', [ 0 ]);                 # to prevent speeds to be altered
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
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 0);
    $config->set('top_solid_layers', 0);            # to prevent solid shells and their speeds
    $config->set('bottom_solid_layers', 0);         # to prevent solid shells and their speeds
    $config->set('brim_width', 5);
    $config->set('support_material_speed', 99);
    $config->set('cooling', [ 0 ]);                 # to prevent speeds to be altered
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
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('skirts', 1);
    $config->set('brim_width', 10);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'successful G-code generation when skirt is smaller than brim width';
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('skirts', 1);
    $config->set('skirt_height', 0);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'successful G-code generation when skirt_height = 0 and skirts > 0';
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', 0.4);
    $config->set('skirts', 1);
    $config->set('skirt_distance', 0);
    $config->set('support_material_speed', 99);
    $config->set('perimeter_extruder', 1);
    $config->set('support_material_extruder', 2);
    $config->set('cooling', [ 0 ]);                 # to prevent speeds to be altered
    $config->set('first_layer_speed', '100%');      # to prevent speeds to be altered
    
    my $print = Slic3r::Test::init_print('overhang', config => $config);
    $print->process;
    
    # we enable support material after skirt has been generated
    $config->set('support_material', 1);
    $print->apply_config($config);
    
    my $skirt_length = 0;
    my @extrusion_points = ();
    my $tool = undef;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
        } elsif (defined $self->Z && $self->Z == $config->first_layer_height) {
            # we're on first layer
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                my $speed = ($args->{F} // $self->F) / 60;
                if ($speed == $config->support_material_speed && $tool == $config->perimeter_extruder-1) {
                    # skirt uses support material speed but first object's extruder
                    $skirt_length += $info->{dist_XY};
                } else {
                    push @extrusion_points, my $point = Slic3r::Point->new_scale($args->{X}, $args->{Y});
                }
            }
        }
    });
    my $convex_hull = convex_hull(\@extrusion_points);
    my $hull_perimeter = unscale($convex_hull->split_at_first_point->length);
    ok $skirt_length > $hull_perimeter, 'skirt lenght is large enough to contain object with support';
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('min_skirt_length', 20);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'no crash when using min_skirt_length';
}

__END__
