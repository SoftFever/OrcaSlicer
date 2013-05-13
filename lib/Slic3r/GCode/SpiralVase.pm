package Slic3r::GCode::SpiralVase;
use Moo;

use Slic3r::Geometry qw(unscale);

sub process_layer {
    my $self = shift;
    my ($gcode, $layer) = @_;
    
    my $total_layer_length = 0;
    Slic3r::GCode::Reader->new(gcode => $gcode)->parse(sub {
        my ($reader, $cmd, $args, $info) = @_;
        $total_layer_length += $info->{dist_XY}
            if $cmd eq 'G1' && $info->{extruding};
    });
    
    my $new_gcode = "";
    my $layer_height = $layer->height;
    my $z = unscale($layer->print_z) - $layer_height;
    Slic3r::GCode::Reader->new(gcode => $gcode)->parse(sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && exists $args->{Z}) {
            my $line = $info->{raw};
            $line =~ s/Z([^ ]+)/Z$z/;
            $new_gcode .= "$line\n";
        } elsif ($cmd eq 'G1' && !exists $args->{Z} && $info->{extruding} && $info->{dist_XY}) {
            $z += $info->{dist_XY} * $layer_height / $total_layer_length;
            my $line = $info->{raw};
            $line =~ s/^G1 /sprintf 'G1 Z%.3f ', $z/e;
            $new_gcode .= "$line\n";
        } else {
            $new_gcode .= "$info->{raw}\n";
        }
    });
    
    return $new_gcode;
}

1;
