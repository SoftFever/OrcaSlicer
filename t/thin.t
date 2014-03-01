use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use List::Util qw(first);
use Slic3r::Geometry qw(epsilon);
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('layer_height', 0.2);
    $config->set('first_layer_height', '100%');
    $config->set('extrusion_width', 0.5);
    $config->set('first_layer_extrusion_width', '200%'); # check this one too
    $config->set('skirts', 0);
    $config->set('thin_walls', 1);
    
    my $print = Slic3r::Test::init_print('gt2_teeth', config => $config);
    
    my %extrusion_paths = ();  # Z => count of continuous extrusions
    my $extruding = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1') {
            if ($info->{extruding} && $info->{dist_XY}) {
                if (!$extruding) {
                    $extrusion_paths{$self->Z} //= 0;
                    $extrusion_paths{$self->Z}++;
                }
                $extruding = 1;
            } else {
                $extruding = 0;
            }
        }
    });
    
    ok !(first { $_ != 3 } values %extrusion_paths),
        'no superfluous thin walls are generated for toothed profile';
}

__END__
