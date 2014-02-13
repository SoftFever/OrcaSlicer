package Slic3r::GCode::SpiralVase;
use Moo;

has 'config' => (is => 'ro', required => 1);
has 'enable' => (is => 'rw', default => sub { 0 });
has 'gcode_reader' => (is => 'ro', default => sub { Slic3r::GCode::Reader->new });

use Slic3r::Geometry qw(unscale);

sub process_layer {
    my $self = shift;
    my ($gcode, $layer) = @_;
    
    # if we're not going to modify G-code, just feed it to the reader
    # in order to update positions
    if (!$self->enable) {
        $self->gcode_reader->parse($gcode, sub {});
        return $gcode;
    }
    
    # get total XY length for this layer by summing all extrusion moves
    my $total_layer_length = 0;
    my $z = undef;
    Slic3r::GCode::Reader->new->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1') {
            $total_layer_length += $info->{dist_XY}
                if $info->{extruding};
            
            # get first Z
            $z //= $args->{Z}
                if exists $args->{Z};
        }
    });
    
    my $new_gcode = "";
    my $layer_height = $layer->height;
    
    # remove layer height from initial Z
    $z -= $layer_height;
    
    my $newlayer = 0;
    $self->gcode_reader->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && exists $args->{Z}) {
            # if this is the initial Z move of the layer, replace it with a
            # (redundant) move to the last Z of previous layer
            my $line = $info->{raw};
            $line =~ s/Z[.0-9]+/Z$z/;
            $new_gcode .= "$line\n";
            $newlayer = 1;
        } elsif ($cmd eq 'G1' && !exists $args->{Z} && $info->{dist_XY}) {
            # horizontal move
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
