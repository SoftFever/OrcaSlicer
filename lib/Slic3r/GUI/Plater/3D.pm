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
    $self->enable_picking(1);
    
    $self->{objects}            = $objects;
    $self->{model}              = $model;
    $self->{config}             = $config;
    $self->{on_select_object}   = sub {};
    $self->{on_double_click}    = sub {};
    $self->{on_right_click}     = sub {};
    $self->{on_instance_moved}  = sub {};
    
    return $self;
}

sub set_on_select_object {
    my ($self, $cb) = @_;
    $self->on_select_object(sub {
        my ($volume_idx) = @_;
        
        return $cb->(undef) if $volume_idx == -1;
        my $obj_idx = $self->{_volumes_inv}{$volume_idx};
        return $cb->($obj_idx);
    });
}

sub set_on_double_click {
    my ($self, $cb) = @_;
    $self->on_double_click($cb);
}

sub set_on_right_click {
    my ($self, $cb) = @_;
    $self->on_right_click($cb);
}

sub set_on_instance_moved {
    my ($self, $cb) = @_;
    $self->on_instance_moved(sub {
        my ($volume_idx, $instance_idx) = @_;
        
        my $obj_idx = $self->{_volumes_inv}{$volume_idx};
        return $cb->($obj_idx, $instance_idx);
    });
}

sub update {
    my ($self) = @_;
    
    $self->{_volumes} = {};     # obj_idx => [ volume_idx, volume_idx ]
    $self->{_volumes_inv} = {}; # volume_idx => obj_idx
    $self->reset_objects;
    return if $self->{model}->objects_count == 0;
    
    $self->set_bounding_box($self->{model}->bounding_box);
    $self->set_bed_shape($self->{config}->bed_shape);
    
    foreach my $obj_idx (0..$#{$self->{model}->objects}) {
        my $model_object = $self->{model}->get_object($obj_idx);
        my @volume_idxs = $self->load_object($model_object, 1);
        
        # store mapping between canvas volumes and model objects
        $self->{_volumes}{$obj_idx} = [ @volume_idxs ];
        $self->{_volumes_inv}{$_} = $obj_idx for @volume_idxs;
        
        if ($self->{objects}[$obj_idx]{selected}) {
            $self->select_volume($_) for @volume_idxs;
        }
    }
}

1;