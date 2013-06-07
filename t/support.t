use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('raft_layers', 3);
    $config->set('brim_width',  6);
    $config->set('skirts', 0);
    $config->set('support_material_extruder', 2);
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', '100%');
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok my $gcode = Slic3r::Test::gcode($print), 'no conflict between raft/support and brim';
    
    my $tool = 0;
    Slic3r::GCode::Reader->new(gcode => $gcode)->parse(sub {
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
