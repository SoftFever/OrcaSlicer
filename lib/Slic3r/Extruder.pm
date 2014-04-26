package Slic3r::Extruder;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK   = qw(EXTRUDER_ROLE_PERIMETER EXTRUDER_ROLE_INFILL EXTRUDER_ROLE_SUPPORT_MATERIAL
                    EXTRUDER_ROLE_SUPPORT_MATERIAL_INTERFACE);
our %EXPORT_TAGS = (roles => \@EXPORT_OK);

use Slic3r::Geometry qw(PI scale);

use constant OPTIONS => [qw(
    extruder_offset
    nozzle_diameter filament_diameter extrusion_multiplier temperature first_layer_temperature
    retract_length retract_lift retract_speed retract_restart_extra retract_before_travel
    retract_layer_change retract_length_toolchange retract_restart_extra_toolchange wipe
)];

# has 'e_per_mm3'                 => (is => 'lazy');
# has 'retract_speed_mm_min'      => (is => 'lazy');

use constant EXTRUDER_ROLE_PERIMETER                    => 1;
use constant EXTRUDER_ROLE_INFILL                       => 2;
use constant EXTRUDER_ROLE_SUPPORT_MATERIAL             => 3;
use constant EXTRUDER_ROLE_SUPPORT_MATERIAL_INTERFACE   => 4;


# generate accessors
{
    no strict 'refs';
    for my $opt_key (@{&Slic3r::Extruder::OPTIONS}) {
        *{$opt_key} = sub {
            my $self = shift;
            $self->config->get_at($opt_key, $self->id);
        };
    }
}


sub e_per_mm3 {
    my $self = shift;
    return $self->extrusion_multiplier * (4 / (($self->filament_diameter ** 2) * PI));
}

sub retract_speed_mm_min {
    my $self = shift;
    return $self->retract_speed * 60;
}

sub scaled_wipe_distance {
    my ($self, $travel_speed) = @_;
    
    # how far do we move in XY at travel_speed for the time needed to consume
    # retract_length at retract_speed?
    # reduce feedrate a bit; travel speed is often too high to move on existing material
    # too fast = ripping of existing material; too slow = short wipe path, thus more blob
    return scale($self->retract_length / $self->retract_speed * $travel_speed * 0.8);
}

sub extruded_volume {
    my ($self, $E) = @_;
    return $E * ($self->filament_diameter**2) * PI/4;
}

sub e_per_mm {
    my ($self, $mm3_per_mm) = @_;
    return $mm3_per_mm * $self->e_per_mm3;
}

1;
