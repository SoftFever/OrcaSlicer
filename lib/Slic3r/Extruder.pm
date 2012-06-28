package Slic3r::Extruder;
use Moo;

use Slic3r::Geometry qw(PI);

has 'nozzle_diameter'           => (is => 'rw', required => 1);
has 'filament_diameter'         => (is => 'rw', required => 1);
has 'extrusion_multiplier'      => (is => 'rw', required => 1);
has 'temperature'               => (is => 'rw', required => 1);
has 'first_layer_temperature'   => (is => 'rw', required => 1);

has 'e_per_mmc'             => (is => 'rw');

sub BUILD {
    my $self = shift;
    $self->e_per_mmc(
        $Slic3r::scaling_factor
        * $self->extrusion_multiplier
        * (4 / (($self->filament_diameter ** 2) * PI))
    );
}

sub make_flow {
    my $self = shift;
    return Slic3r::Flow->new(nozzle_diameter => $self->nozzle_diameter, @_);
}

1;
