package Slic3r::GCode::SpiralVase;
use Moo;

has 'config' => (is => 'ro', required => 1);

use Slic3r::Geometry qw(unscale);

sub process_layer {
    my $self = shift;
    my ($gcode, $layer) = @_;
    
    my $total_layer_length = 0;
    Slic3r::GCode::Reader->new->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        $total_layer_length += $info->{dist_XY}
            if $cmd eq 'G1' && $info->{extruding};
    });
    
    my $new_gcode = "";
    my $layer_height = $layer->height;
    my $z = $layer->print_z + $self->config->z_offset - $layer_height;
    my $newlayer = 0;
    Slic3r::GCode::Reader->new->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && exists $args->{Z}) {
            my $line = $info->{raw};
            $line =~ s/Z([^ ]+)/Z$z/;
            $new_gcode .= "$line\n";
            $newlayer = 1;
        } elsif ($cmd eq 'G1' && !exists $args->{Z} && $info->{dist_XY}) {
            my $line = $info->{raw};
            if ($info->{extruding}) {
                $z += $info->{dist_XY} * $layer_height / $total_layer_length;
                $line =~ s/^G1 /sprintf 'G1 Z%.3f ', $z/e;
                $new_gcode .= "$line\n";
            } elsif ($newlayer) {
                # remove the first travel move after layer change; extrusion
                # will just blend to the first loop vertex
                # TODO: should we adjust (stretch) E for the first loop segment?
                $newlayer = 0;
            } else {
                $new_gcode .= "$line\n";
            }
        } else {
            $new_gcode .= "$info->{raw}\n";
        }
    });
    
    return $new_gcode;
}

1;
