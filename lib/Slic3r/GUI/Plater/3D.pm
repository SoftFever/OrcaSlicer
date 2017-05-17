package Slic3r::GUI::Plater::3D;
use strict;
use warnings;
use utf8;

use List::Util qw();
use Slic3r::Geometry qw();
use Slic3r::Geometry::Clipper qw();
use Wx qw(:misc :pen :brush :sizer :font :cursor wxTAB_TRAVERSAL);
use Wx::Event qw();
use base qw(Slic3r::GUI::3DScene Class::Accessor);

sub new {
    my $class = shift;
    my ($parent, $objects, $model, $print, $config) = @_;
    
    my $self = $class->SUPER::new($parent);
    $self->enable_picking(1);
    $self->enable_moving(1);
    $self->select_by('object');
    $self->drag_by('instance');
    
    $self->{objects}            = $objects;
    $self->{model}              = $model;
    $self->{print}              = $print;
    $self->{config}             = $config;
    $self->{on_select_object}   = sub {};
    $self->{on_instances_moved} = sub {};
    
    $self->on_select(sub {
        my ($volume_idx) = @_;
        $self->{on_select_object}->(($volume_idx == -1) ? undef : $self->volumes->[$volume_idx]->object_idx)
            if ($self->{on_select_object});
    });
    $self->on_move(sub {
        my @volume_idxs = @_;
        
        my %done = ();  # prevent moving instances twice
        foreach my $volume_idx (@volume_idxs) {
            my $volume = $self->volumes->[$volume_idx];
            my $obj_idx = $volume->object_idx;
            my $instance_idx = $volume->instance_idx;
            next if $done{"${obj_idx}_${instance_idx}"};
            $done{"${obj_idx}_${instance_idx}"} = 1;
            if ($obj_idx < 1000) {
                # Move a regular object.
                my $model_object = $self->{model}->get_object($obj_idx);
                $model_object
                    ->instances->[$instance_idx]
                    ->offset
                    ->translate($volume->origin->x, $volume->origin->y); #))
                $model_object->invalidate_bounding_box;
            } elsif ($obj_idx == 1000) {
                # Move a wipe tower proxy.
            }
        }
        
        $self->{on_instances_moved}->()
            if $self->{on_instances_moved};
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

sub set_on_instances_moved {
    my ($self, $cb) = @_;
    $self->{on_instances_moved} = $cb;
}

sub set_on_model_update {
    my ($self, $cb) = @_;
    $self->on_model_update($cb);
}

sub reload_scene {
    my ($self) = @_;

    if (0) {
        my $i = 1;
        print STDERR "3D::reload_scene - Stack Trace:\n";
        while ( (my @call_details = (caller($i++))) ){
            print STDERR $call_details[1].":".$call_details[2]." in function ".$call_details[3]."\n";
        }
    }
    
    $self->reset_objects;
    $self->update_bed_size;
    
    foreach my $obj_idx (0..$#{$self->{model}->objects}) {
        my @volume_idxs = $self->load_object($self->{model}, $self->{print}, $obj_idx);
        if ($self->{objects}[$obj_idx]->selected) {
            $self->select_volume($_) for @volume_idxs;
        }
    }
    if (0) {
        print "Config: $self->{config}\n";
        $self->{config}->save('d:\temp\cfg.ini');
    }
    if (defined $self->{config}->nozzle_diameter) {
        # Should the wipe tower be visualized?
        my $extruders_count = scalar @{ $self->{config}->nozzle_diameter };
        # Height of a print.
        my $height = $self->{model}->bounding_box->z_max;
        # Show at least a slab.
        $height = 10 if $height < 10;
        if ($extruders_count > 1 && $self->{config}->single_extruder_multi_material && $self->{config}->wipe_tower &&
            ! $self->{config}->complete_objects) {
            $self->volumes->load_wipe_tower_preview(1000, 
                $self->{config}->wipe_tower_x, $self->{config}->wipe_tower_y, 
                $self->{config}->wipe_tower_width, $self->{config}->wipe_tower_per_color_wipe * ($extruders_count - 1),
                $self->{model}->bounding_box->z_max, $self->UseVBOs);
        }
    }
}

sub update_bed_size {
    my ($self) = @_;
    $self->set_bed_shape($self->{config}->bed_shape);
}

1;