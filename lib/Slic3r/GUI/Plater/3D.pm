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
    $self->enable_moving(1);
    
    $self->{objects}            = $objects;
    $self->{model}              = $model;
    $self->{config}             = $config;
    $self->{on_select_object}   = sub {};
    $self->{on_instance_moved}  = sub {};
    
    $self->on_select(sub {
        my ($volume_idx) = @_;
        
        my $obj_idx = undef;
        if ($volume_idx != -1) {
            $obj_idx = $self->{_volumes_inv}{$volume_idx};
            $self->volumes->[$_]->selected(1) for @{$self->{_volumes}{$obj_idx}};
            $self->Refresh;
        }
        $self->{on_select_object}->($obj_idx)
            if $self->{on_select_object};
    });
    $self->on_hover(sub {
        my ($volume_idx) = @_;
        
        my $obj_idx = $self->{_volumes_inv}{$volume_idx};
        $self->volumes->[$_]->hover(1) for @{$self->{_volumes}{$obj_idx}};
    });
    $self->on_move(sub {
        my ($volume_idx) = @_;
        
        my $volume = $self->volumes->[$volume_idx];
        my $obj_idx = $self->{_volumes_inv}{$volume_idx};
        my $model_object = $self->{model}->get_object($obj_idx);
        $model_object
            ->instances->[$volume->instance_idx]
            ->offset
            ->translate($volume->origin->x, $volume->origin->y); #))
        $model_object->invalidate_bounding_box;
        
        $self->{on_instance_moved}->($obj_idx, $volume->instance_idx)
            if $self->{on_instance_moved};
    });
    
    return $self;
}

sub set_on_select_object {
    my ($self, $cb) = @_;
    $self->{on_select_object} = $cb;
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
    $self->{on_instance_moved} = $cb;
}

sub update {
    my ($self) = @_;
    
    $self->{_volumes} = {};     # obj_idx => [ volume_idx, volume_idx ]
    $self->{_volumes_inv} = {}; # volume_idx => obj_idx
    $self->reset_objects;
    
    $self->update_bed_size;
    
    foreach my $obj_idx (0..$#{$self->{model}->objects}) {
        my $model_object = $self->{model}->get_object($obj_idx);
        my @volume_idxs = $self->load_object($model_object, 1);
        
        # store mapping between canvas volumes and model objects
        $self->{_volumes}{$obj_idx} = [ @volume_idxs ];
        $self->{_volumes_inv}{$_} = $obj_idx for @volume_idxs;
        
        if ($self->{objects}[$obj_idx]->selected) {
            $self->select_volume($_) for @volume_idxs;
        }
    }
}

sub update_bed_size {
    my ($self) = @_;
    $self->set_bed_shape($self->{config}->bed_shape);
}

1;