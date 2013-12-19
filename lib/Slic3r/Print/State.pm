package Slic3r::Print::State;
use Moo;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK   = qw(STEP_INIT_EXTRUDERS STEP_SLICE STEP_PERIMETERS STEP_PREPARE_INFILL 
                    STEP_INFILL STEP_SUPPORTMATERIAL STEP_SKIRT STEP_BRIM);
our %EXPORT_TAGS = (steps => \@EXPORT_OK);

has '_started'  => (is => 'ro', default => sub {{}});  # { step => 1, ... }
has '_done'     => (is => 'ro', default => sub {{}});  # { step => 1, ... }

use constant STEP_INIT_EXTRUDERS    => 0;
use constant STEP_SLICE             => 1;
use constant STEP_PERIMETERS        => 2;
use constant STEP_PREPARE_INFILL    => 3;
use constant STEP_INFILL            => 4;
use constant STEP_SUPPORTMATERIAL   => 5;
use constant STEP_SKIRT             => 6;
use constant STEP_BRIM              => 7;

our %print_steps = map { $_ => 1 } (
    STEP_INIT_EXTRUDERS,
    STEP_SKIRT,
    STEP_BRIM,
);

our %prereqs = (
    STEP_INIT_EXTRUDERS     => [],
    STEP_SLICE              => [],
    STEP_PERIMETERS         => [STEP_SLICE, STEP_INIT_EXTRUDERS],
    STEP_PREPARE_INFILL     => [STEP_PERIMETERS],
    STEP_INFILL             => [STEP_INFILL],
    STEP_SUPPORTMATERIAL    => [STEP_SLICE, STEP_INIT_EXTRUDERS],
    STEP_SKIRT              => [STEP_PERIMETERS, STEP_INFILL],
    STEP_BRIM               => [STEP_PERIMETERS, STEP_INFILL, STEP_SKIRT],
);

sub started {
    my ($self, $step) = @_;
    return $self->_started->{$step};
}

sub done {
    my ($self, $step) = @_;
    return $self->_done->{$step};
}

sub set_started {
    my ($self, $step) = @_;
    
    $self->_started->{$step} = 1;
    delete $self->_done->{$step};
}

sub set_done {
    my ($self, $step) = @_;
    $self->_done->{$step} = 1;
}

sub invalidate {
    my ($self, $step) = @_;
    
    delete $self->_started->{$step};
    delete $self->_done->{$step};
}

1;
