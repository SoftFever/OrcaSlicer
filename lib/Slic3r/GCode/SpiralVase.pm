package Slic3r::GCode::SpiralVase;
use Moo;

has 'config' => (is => 'ro', required => 1);
has 'enable' => (is => 'rw', default => sub { 0 });
has 'reader' => (is => 'ro', default => sub { Slic3r::GCode::Reader->new });

use Slic3r::Geometry qw(unscale);

sub BUILD {
    my ($self) = @_;
    $self->reader->apply_print_config($self->config);
}

sub process_layer {
    my $self = shift;
    my ($gcode) = @_;
    
    # This post-processor relies on several assumptions:
    # - all layers are processed through it, including those that are not supposed
    #   to be transformed, in order to update the reader with the XY positions
    # - each call to this method includes a full layer, with a single Z move
    #   at the beginning
    # - each layer is composed by suitable geometry (i.e. a single complete loop)
    # - loops were not clipped before calling this method
    
    # if we're not going to modify G-code, just feed it to the reader
    # in order to update positions
    if (!$self->enable) {
        $self->reader->parse($gcode, sub {});
        return $gcode;
    }
    
    # get total XY length for this layer by summing all extrusion moves
    my $total_layer_length = 0;
    my $layer_height = 0;
    my $z = undef;
    $self->reader->clone->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1') {
            if ($info->{extruding}) {
                $total_layer_length += $info->{dist_XY};
            } elsif (exists $args->{Z}) {
                $layer_height += $info->{dist_Z};
                $z //= $args->{Z};
            }
        }
    });
    
    #use XXX; XXX [ $gcode, $layer_height, $z, $total_layer_length ];
    # remove layer height from initial Z
    $z -= $layer_height;
    
    my $new_gcode = "";
    $self->reader->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && exists $args->{Z}) {
            # if this is the initial Z move of the layer, replace it with a
            # (redundant) move to the last Z of previous layer
            my $line = $info->{raw};
            $line =~ s/ Z[.0-9]+/ Z$z/;
            $new_gcode .= "$line\n";
        } elsif ($cmd eq 'G1' && !exists($args->{Z}) && $info->{dist_XY}) {
            # horizontal move
            my $line = $info->{raw};
            if ($info->{extruding}) {
                $z += $info->{dist_XY} * $layer_height / $total_layer_length;
                $line =~ s/^G1 /sprintf 'G1 Z%.3f ', $z/e;
                $new_gcode .= "$line\n";
            }
            # skip travel moves: the move to first perimeter point will
            # cause a visible seam when loops are not aligned in XY; by skipping
            # it we blend the first loop move in the XY plane (although the smoothness
            # of such blend depend on how long the first segment is; maybe we should
            # enforce some minimum length?)
        } else {
            $new_gcode .= "$info->{raw}\n";
        }
    });
    
    return $new_gcode;
}

1;
