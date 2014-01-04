package Slic3r::Extruder;
use Moo;

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

has 'id'    => (is => 'rw', required => 1);
has $_      => (is => 'ro', required => 1) for @{&OPTIONS};
has 'use_relative_e_distances'  => (is => 'ro', default => sub {0});

has 'E'                         => (is => 'rw', default => sub {0} );
has 'absolute_E'                => (is => 'rw', default => sub {0} );
has 'retracted'                 => (is => 'rw', default => sub {0} );
has 'restart_extra'             => (is => 'rw', default => sub {0} );
has 'e_per_mm3'                 => (is => 'lazy');
has 'retract_speed_mm_min'      => (is => 'lazy');

use constant EXTRUDER_ROLE_PERIMETER                    => 1;
use constant EXTRUDER_ROLE_INFILL                       => 2;
use constant EXTRUDER_ROLE_SUPPORT_MATERIAL             => 3;
use constant EXTRUDER_ROLE_SUPPORT_MATERIAL_INTERFACE   => 4;

sub new_from_config {
    my ($class, $config, $extruder_id) = @_;
    
    my %conf = (
        id => $extruder_id,
        use_relative_e_distances => $config->use_relative_e_distances,
    );
    foreach my $opt_key (@{&OPTIONS}) {
        $conf{$opt_key} = $config->get_at($opt_key, $extruder_id);
    }
    return $class->new(%conf);
}

sub _build_e_per_mm3 {
    my $self = shift;
    return $self->extrusion_multiplier * (4 / (($self->filament_diameter ** 2) * PI));
}

sub _build_retract_speed_mm_min {
    my $self = shift;
    return $self->retract_speed * 60;
}

sub reset {
    my ($self) = @_;
    
    $self->E(0);
    $self->absolute_E(0);
    $self->retracted(0);
    $self->restart_extra(0);
}

sub scaled_wipe_distance {
    my ($self, $travel_speed) = @_;
    
    # how far do we move in XY at travel_speed for the time needed to consume
    # retract_length at retract_speed?
    # reduce feedrate a bit; travel speed is often too high to move on existing material
    # too fast = ripping of existing material; too slow = short wipe path, thus more blob
    return scale($self->retract_length / $self->retract_speed * $travel_speed * 0.8);
}

sub extrude {
    my ($self, $E) = @_;
    
    $self->E(0) if $self->use_relative_e_distances;
    $self->absolute_E($self->absolute_E + $E);
    return $self->E($self->E + $E);
}

sub extruded_volume {
    my ($self) = @_;
    return $self->absolute_E * ($self->filament_diameter**2) * PI/4;
}

sub e_per_mm {
    my ($self, $mm3_per_mm) = @_;
    return $mm3_per_mm * $self->e_per_mm3;
}

1;
