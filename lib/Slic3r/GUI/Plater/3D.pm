package Slic3r::GUI::Plater::3D;
use strict;
use warnings;
use utf8;

use List::Util qw();
use Slic3r::Geometry qw();
use Slic3r::Geometry::Clipper qw();
use Wx qw(:misc :pen :brush :sizer :font :cursor wxTAB_TRAVERSAL);
use Wx::Event qw();
use base 'Slic3r::GUI::PreviewCanvas';

sub new {
    my $class = shift;
    my ($parent, $objects, $model, $config) = @_;
    
    my $self = $class->SUPER::new($parent);
    
    $self->{objects}            = $objects;
    $self->{model}              = $model;
    $self->{config}             = $config;
    $self->{on_select_object}   = sub {};
    $self->{on_double_click}    = sub {};
    $self->{on_right_click}     = sub {};
    $self->{on_instance_moved}  = sub {};
    
    
    
    return $self;
}

sub update {
    my ($self) = @_;
    
    $self->reset_objects;
    return if $self->{model}->objects_count == 0;
    
    $self->set_bounding_box($self->{model}->bounding_box);
    $self->set_bed_shape($self->{config}->bed_shape);
    
    foreach my $model_object (@{$self->{model}->objects}) {
        $self->load_object($model_object, 1);
    }
}

1;
