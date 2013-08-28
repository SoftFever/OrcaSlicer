use Test::More tests => 1;
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
    $config->set('perimeter_speed', 99);
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
                    if $info->{extruding} && ($args->{F} // $self->F) == $config->perimeter_speed*60;
            }
        });
        fail "wrong number of layers with skirt"
            unless (grep $_, values %layers_with_skirt) == $config->skirt_height;
    };
    
    ok $test->(), "skirt_height is honored when printing multiple objects too";
}

__END__
