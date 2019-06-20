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
use Slic3r::Geometry qw(unscale X Y);
use Slic3r::Test;

{
    my $config = Slic3r::Config::new_from_defaults;
    my $print_center = [100,100];
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config, print_center => $print_center);
    my @extrusion_points = ();
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            push @extrusion_points, my $point = Slic3r::Point->new_scale($args->{X}, $args->{Y});
        }
    });
    my $bb = Slic3r::Geometry::BoundingBox->new_from_points(\@extrusion_points);
    my $center = $bb->center;
    ok abs(unscale($center->[X]) - $print_center->[X]) < 0.005, 'print is centered around print_center (X)';
    ok abs(unscale($center->[Y]) - $print_center->[Y]) < 0.005, 'print is centered around print_center (Y)';
}

{
    # this represents the aggregate config from presets
    my $config = Slic3r::Config::new_from_defaults;
    # Define 4 extruders.
    $config->set('nozzle_diameter', [0.4, 0.4, 0.4, 0.4]);
    
    # user adds one object to the plater
    my $print = Slic3r::Test::init_print(my $model = Slic3r::Test::model('20mm_cube'), config => $config);
    
    # user sets a per-region option
    my $model2 = $model->clone;
    $model2->get_object(0)->config->set('fill_density', 100);
    $print->apply($model2, $config);
    
    is $print->print->regions->[0]->config->fill_density, 100, 'region config inherits model object config';
    
    # user exports G-code, thus the default config is reapplied
    $model2->get_object(0)->config->erase('fill_density');
    $print->apply($model2, $config);

    is $print->print->regions->[0]->config->fill_density, 20, 'region config is resetted';
    
    # user assigns object extruders
    $model2->get_object(0)->config->set('extruder', 3);
    $model2->get_object(0)->config->set('perimeter_extruder', 2);
    $print->apply($model2, $config);

    is $print->print->regions->[0]->config->infill_extruder, 3, 'extruder setting is correctly expanded';
    is $print->print->regions->[0]->config->perimeter_extruder, 2, 'extruder setting does not override explicitely specified extruders';
}

__END__
