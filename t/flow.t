use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first sum);
use Slic3r;
use Slic3r::Geometry qw(scale);
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 1);
    $config->set('brim_width', 2);
    $config->set('perimeters', 3);
    $config->set('fill_density', 0.4);
    $config->set('bottom_solid_layers', 1);
    $config->set('first_layer_extrusion_width', 2);
    $config->set('first_layer_height', '100%');
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my @E_per_mm = ();
    Slic3r::GCode::Reader->new->parse(my $gcode = Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($self->Z == $config->layer_height) {  # only consider first layer
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                push @E_per_mm, $info->{dist_E} / $info->{dist_XY};
            }
        }
    });
    my $E_per_mm_avg = sum(@E_per_mm) / @E_per_mm;
    ok !(defined first { abs($_ - $E_per_mm_avg) > 0.01 } @E_per_mm),
        'first_layer_extrusion_width applies to everything on first layer';
}

__END__
