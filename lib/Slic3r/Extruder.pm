package Slic3r::Extruder;
use Moo;

use Slic3r::Geometry qw(PI);

has 'nozzle_diameter'           => (is => 'ro', required => 1);
has 'filament_diameter'         => (is => 'ro', required => 1);
has 'extrusion_multiplier'      => (is => 'ro', required => 1);
has 'temperature'               => (is => 'ro', required => 1);
has 'first_layer_temperature'   => (is => 'rw', required => 1);

has 'e_per_mm3'                 => (is => 'lazy');
has '_mm3_per_mm_cache'         => (is => 'ro', default => sub {{}});

sub _build_e_per_mm3 {
    my $self = shift;
    return $self->extrusion_multiplier * (4 / (($self->filament_diameter ** 2) * PI));
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

1;
