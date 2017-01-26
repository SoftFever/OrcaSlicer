# A pure perl (no C++ implementation) G-code filter, to control the pressure inside the nozzle.

package Slic3r::GCode::PressureRegulator;
use Moo;

has 'config'            => (is => 'ro', required => 1);
has 'enable'            => (is => 'rw', default => sub { 0 });
has 'reader'            => (is => 'ro', default => sub { Slic3r::GCode::Reader->new });
has '_extrusion_axis'   => (is => 'rw', default => sub { "E" });
has '_tool'             => (is => 'rw', default => sub { 0 });
has '_last_print_F'     => (is => 'rw', default => sub { 0 });
has '_advance'          => (is => 'rw', default => sub { 0 });   # extra E injected

use Slic3r::Geometry qw(epsilon);

# Acknowledgements:
# The advance algorithm was proposed by Matthew Roberts.
# The initial work on this Slic3r feature was done by Luís Andrade (lluis)

sub BUILD {
    my ($self) = @_;
    
    $self->reader->apply_print_config($self->config);
    $self->_extrusion_axis($self->config->get_extrusion_axis);
}

sub process {
    my $self = shift;
    my ($gcode, $flush) = @_;
    
    my $new_gcode = "";
    
    $self->reader->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($cmd =~ /^T(\d+)/) {
            $self->_tool($1);
        } elsif ($info->{extruding} && $info->{dist_XY} > 0) {
            # This is a print move.
            my $F = $args->{F} // $reader->F;
            if ($F != $self->_last_print_F || ($F == $self->_last_print_F && $self->_advance == 0)) {
                # We are setting a (potentially) new speed or a discharge event happend since the last speed change, so we calculate the new advance amount.
            
                # First calculate relative flow rate (mm of filament over mm of travel)
                my $rel_flow_rate = $info->{dist_E} / $info->{dist_XY};
            
                # Then calculate absolute flow rate (mm/sec of feedstock)
                my $flow_rate = $rel_flow_rate * $F / 60;
            
                # And finally calculate advance by using the user-configured K factor.
                my $new_advance = $self->config->pressure_advance * ($flow_rate**2);
                
                if (abs($new_advance - $self->_advance) > 1E-5) {
                    my $new_E = ($self->config->use_relative_e_distances ? 0 : $reader->E) + ($new_advance - $self->_advance);
                    $new_gcode .= sprintf "G1 %s%.5f F%.3f ; pressure advance\n",
                        $self->_extrusion_axis, $new_E, $self->_unretract_speed;
                    $new_gcode .= sprintf "G92 %s%.5f ; restore E\n", $self->_extrusion_axis, $reader->E
                        if !$self->config->use_relative_e_distances;
					$new_gcode .= sprintf "G1 F%.3f ; restore F\n", $F;
                    $self->_advance($new_advance);
                }
                
                $self->_last_print_F($F);
            }
        } elsif (($info->{retracting} || $cmd eq 'G10') && $self->_advance != 0) {
            # We need to bring pressure to zero when retracting.
            $new_gcode .= $self->_discharge($args->{F}, $args->{F} // $reader->F);
        }
        
        $new_gcode .= "$info->{raw}\n";
    });
    
    if ($flush) {
        $new_gcode .= $self->_discharge;
    }
    
    return $new_gcode;
}

sub _discharge {
    my ($self, $F, $oldSpeed) = @_;
    
    my $new_E = ($self->config->use_relative_e_distances ? 0 : $self->reader->E) - $self->_advance;
    my $gcode = sprintf "G1 %s%.5f F%.3f ; pressure discharge\n",
        $self->_extrusion_axis, $new_E, $F // $self->_unretract_speed;
    $gcode .= sprintf "G92 %s%.5f ; restore E\n", $self->_extrusion_axis, $self->reader->E
        if !$self->config->use_relative_e_distances;
    $gcode .= sprintf "G1 F%.3f ; restore F\n", $oldSpeed 
        if $oldSpeed;
    $self->_advance(0);
    
    return $gcode;
}

sub _unretract_speed {
    my ($self) = @_;
    return $self->config->get_at('retract_speed', $self->_tool) * 60;
}

1;
