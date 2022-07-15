use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(PI scale unscale convex_hull);
use Slic3r::Surface ':types';
use Slic3r::Test;

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeter_speed', 66);
    $config->set('external_perimeter_speed', 66);
    $config->set('small_perimeter_speed', 66);
    $config->set('gap_fill_speed', 99);
    $config->set('perimeters', 1);
    $config->set('cooling', [ 0 ]);                 # to prevent speeds from being altered
    $config->set('first_layer_speed', '100%');      # to prevent speeds from being altered
    $config->set('perimeter_extrusion_width', 0.35);
    $config->set('first_layer_extrusion_width', 0.35);
    
    my $print = Slic3r::Test::init_print('two_hollow_squares', config => $config);
    my @perimeter_points = ();
    my $last = '';  # perimeter | gap
    my $gap_fills_outside_last_perimeters = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{extruding} && $info->{dist_XY} > 0) {
            my $F = $args->{F} // $self->F;
            my $point = Slic3r::Point->new_scale($info->{new_X}, $info->{new_Y});
            if ($F == $config->perimeter_speed*60) {
                if ($last eq 'gap') {
                    @perimeter_points = ();
                }
                push @perimeter_points, $point;
                $last = 'perimeter';
            } elsif ($F == $config->gap_fill_speed*60) {
                my $convex_hull = convex_hull(\@perimeter_points);
                if (!$convex_hull->contains_point($point)) {
                    $gap_fills_outside_last_perimeters++;
                }
                
                $last = 'gap';
            }
        }
    });
    is $gap_fills_outside_last_perimeters, 0, 'gap fills are printed before leaving islands';
}

__END__
