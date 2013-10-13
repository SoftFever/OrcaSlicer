package Slic3r::Extruder;
use Moo;

use Slic3r::Geometry qw(PI scale);

use constant OPTIONS => [qw(
    extruder_offset
    nozzle_diameter filament_diameter extrusion_multiplier temperature first_layer_temperature
    retract_length retract_lift retract_speed retract_restart_extra retract_before_travel
    retract_layer_change retract_length_toolchange retract_restart_extra_toolchange wipe
)];

has 'id'    => (is => 'rw', required => 1);
has $_      => (is => 'ro', required => 1) for @{&OPTIONS};
has 'config'=> (is => 'ro', required => 1);

has 'bridge_flow'               => (is => 'lazy');
has 'E'                         => (is => 'rw', default => sub {0} );
has 'absolute_E'                => (is => 'rw', default => sub {0} );
has 'retracted'                 => (is => 'rw', default => sub {0} );
has 'restart_extra'             => (is => 'rw', default => sub {0} );
has 'e_per_mm3'                 => (is => 'lazy');
has 'retract_speed_mm_min'      => (is => 'lazy');
has 'scaled_wipe_distance'      => (is => 'lazy'); # scaled mm
has '_mm3_per_mm_cache'         => (is => 'ro', default => sub {{}});

sub _build_bridge_flow {
    my $self = shift;
    return Slic3r::Flow::Bridge->new(nozzle_diameter => $self->nozzle_diameter);
}

sub _build_e_per_mm3 {
    my $self = shift;
    return $self->extrusion_multiplier * (4 / (($self->filament_diameter ** 2) * PI));
}

sub _build_retract_speed_mm_min {
    my $self = shift;
    return $self->retract_speed * 60;
}

sub _build_scaled_wipe_distance {
    my $self = shift;
    
    # how far do we move in XY at travel_speed for the time needed to consume
    # retract_length at retract_speed?
    # reduce feedrate a bit; travel speed is often too high to move on existing material
    # too fast = ripping of existing material; too slow = short wipe path, thus more blob
    return scale($self->retract_length / $self->retract_speed * $Slic3r::Config->travel_speed * 0.8);
}

sub extrude {
    my ($self, $E) = @_;
    
    $self->E(0) if $self->config->use_relative_e_distances;
    $self->absolute_E($self->absolute_E + $E);
    return $self->E($self->E + $E);
}

sub extruded_volume {
    my ($self) = @_;
    return $self->absolute_E * ($self->filament_diameter**2) * PI/4;
}

sub make_flow {
    my $self = shift;
    return Slic3r::Flow->new(nozzle_diameter => $self->nozzle_diameter, @_);
}

sub mm3_per_mm {
    my $self = shift;
    my ($s, $h) = @_;
    
    my $cache_key = "${s}_${h}";
    if (!exists $self->_mm3_per_mm_cache->{$cache_key}) {
        my $w_threshold = $h + $self->nozzle_diameter;
        my $s_threshold = $w_threshold - &Slic3r::OVERLAP_FACTOR * ($w_threshold - ($w_threshold - $h * (1 - PI/4)));
        
        if ($s >= $s_threshold) {
            # rectangle with semicircles at the ends
            my $w = $s + &Slic3r::OVERLAP_FACTOR * $h * (1 - PI/4);
            $self->_mm3_per_mm_cache->{$cache_key} = $w * $h + ($h**2) / 4 * (PI - 4);
        } else {
            # rectangle with shrunk semicircles at the ends
            my $w = ($s + $self->nozzle_diameter * &Slic3r::OVERLAP_FACTOR * (PI/4 - 1)) / (1 + &Slic3r::OVERLAP_FACTOR * (PI/4 - 1));
            $self->_mm3_per_mm_cache->{$cache_key} = $self->nozzle_diameter * $h * (1 - PI/4) + $h * $w * PI/4;
        }
    }
    return $self->_mm3_per_mm_cache->{$cache_key};
}

sub e_per_mm {
    my $self = shift;
    my ($s, $h) = @_;
    return $self->mm3_per_mm($s, $h) * $self->e_per_mm3;
}

1;
