package Slic3r::GUI::Plater::3D;
use strict;
use warnings;
use utf8;

use List::Util qw();
use Wx qw(:misc :pen :brush :sizer :font :cursor :keycode wxTAB_TRAVERSAL);
#==============================================================================================================================
#use Wx::Event qw(EVT_KEY_DOWN EVT_CHAR);
#==============================================================================================================================
use base qw(Slic3r::GUI::3DScene Class::Accessor);

#==============================================================================================================================
#use Wx::Locale gettext => 'L';
#
#__PACKAGE__->mk_accessors(qw(
#    on_arrange on_rotate_object_left on_rotate_object_right on_scale_object_uniformly
#    on_remove_object on_increase_objects on_decrease_objects on_enable_action_buttons));
#==============================================================================================================================

sub new {
    my $class = shift;
    my ($parent, $objects, $model, $print, $config) = @_;
    
    my $self = $class->SUPER::new($parent);
#==============================================================================================================================
    Slic3r::GUI::_3DScene::enable_picking($self, 1);
    Slic3r::GUI::_3DScene::enable_moving($self, 1);
    Slic3r::GUI::_3DScene::set_select_by($self, 'object');
    Slic3r::GUI::_3DScene::set_drag_by($self, 'instance');
    Slic3r::GUI::_3DScene::set_model($self, $model);
    Slic3r::GUI::_3DScene::set_print($self, $print);
    Slic3r::GUI::_3DScene::set_config($self, $config);
#    $self->enable_picking(1);
#    $self->enable_moving(1);
#    $self->select_by('object');
#    $self->drag_by('instance');
#    
#    $self->{objects}            = $objects;
#    $self->{model}              = $model;
#    $self->{print}              = $print;
#    $self->{config}             = $config;
#    $self->{on_select_object}   = sub {};
#    $self->{on_instances_moved} = sub {};
#    $self->{on_wipe_tower_moved} = sub {};
#
#    $self->{objects_volumes_idxs} = [];
#
#    $self->on_select(sub {
#        my ($volume_idx) = @_;
#        $self->{on_select_object}->(($volume_idx == -1) ? undef : $self->volumes->[$volume_idx]->object_idx)
#            if ($self->{on_select_object});
#    });
#
#    $self->on_move(sub {
#        my @volume_idxs = @_;
#        my %done = ();  # prevent moving instances twice
#        my $object_moved;
#        my $wipe_tower_moved;
#        foreach my $volume_idx (@volume_idxs) {
#            my $volume = $self->volumes->[$volume_idx];
#            my $obj_idx = $volume->object_idx;
#            my $instance_idx = $volume->instance_idx;
#            next if $done{"${obj_idx}_${instance_idx}"};
#            $done{"${obj_idx}_${instance_idx}"} = 1;
#            if ($obj_idx < 1000) {
#                # Move a regular object.
#                my $model_object = $self->{model}->get_object($obj_idx);
#                $model_object
#                    ->instances->[$instance_idx]
#                    ->offset
#                    ->translate($volume->origin->x, $volume->origin->y); #))
#                $model_object->invalidate_bounding_box;
#                $object_moved = 1;
#            } elsif ($obj_idx == 1000) {
#                # Move a wipe tower proxy.
#                $wipe_tower_moved = $volume->origin;
#            }
#        }
#        
#        $self->{on_instances_moved}->()
#            if $object_moved && $self->{on_instances_moved};
#        $self->{on_wipe_tower_moved}->($wipe_tower_moved)
#            if $wipe_tower_moved && $self->{on_wipe_tower_moved};
#    });
#
#    EVT_KEY_DOWN($self, sub {
#        my ($s, $event) = @_;
#        if ($event->HasModifiers) {
#            $event->Skip;
#        } else {
#            my $key = $event->GetKeyCode;
#            if ($key == WXK_DELETE) {
#                $self->on_remove_object->() if $self->on_remove_object;
#            } else {
#                $event->Skip;
#            }
#        }
#    });
#
#    EVT_CHAR($self, sub {
#        my ($s, $event) = @_;
#        if ($event->HasModifiers) {
#            $event->Skip;
#        } else {
#            my $key = $event->GetKeyCode;
#            if ($key == ord('a')) {
#                $self->on_arrange->() if $self->on_arrange;
#            } elsif ($key == ord('l')) {
#                $self->on_rotate_object_left->() if $self->on_rotate_object_left;
#            } elsif ($key == ord('r')) {
#                $self->on_rotate_object_right->() if $self->on_rotate_object_right;
#            } elsif ($key == ord('s')) {
#                $self->on_scale_object_uniformly->() if $self->on_scale_object_uniformly;
#            } elsif ($key == ord('+')) {
#                $self->on_increase_objects->() if $self->on_increase_objects;
#            } elsif ($key == ord('-')) {
#                $self->on_decrease_objects->() if $self->on_decrease_objects;
#            } else {
#                $event->Skip;
#            }
#        }
#    });
#==============================================================================================================================
    
    return $self;
}

#==============================================================================================================================
#sub set_on_select_object {
#    my ($self, $cb) = @_;
#    $self->{on_select_object} = $cb;
#}
#
#sub set_on_double_click {
#    my ($self, $cb) = @_;
#    $self->on_double_click($cb);
#}
#
#sub set_on_right_click {
#    my ($self, $cb) = @_;
#    $self->on_right_click($cb);
#}
#
#sub set_on_arrange {
#    my ($self, $cb) = @_;
#    $self->on_arrange($cb);
#}
#
#sub set_on_rotate_object_left {
#    my ($self, $cb) = @_;
#    $self->on_rotate_object_left($cb);
#}
#
#sub set_on_rotate_object_right {
#    my ($self, $cb) = @_;
#    $self->on_rotate_object_right($cb);
#}
#
#sub set_on_scale_object_uniformly {
#    my ($self, $cb) = @_;
#    $self->on_scale_object_uniformly($cb);
#}
#
#sub set_on_increase_objects {
#    my ($self, $cb) = @_;
#    $self->on_increase_objects($cb);
#}
#
#sub set_on_decrease_objects {
#    my ($self, $cb) = @_;
#    $self->on_decrease_objects($cb);
#}
#
#sub set_on_remove_object {
#    my ($self, $cb) = @_;
#    $self->on_remove_object($cb);
#}
#
#sub set_on_instances_moved {
#    my ($self, $cb) = @_;
#    $self->{on_instances_moved} = $cb;
#}
#
#sub set_on_wipe_tower_moved {
#    my ($self, $cb) = @_;
#    $self->{on_wipe_tower_moved} = $cb;
#}
#
#sub set_on_model_update {
#    my ($self, $cb) = @_;
#    $self->on_model_update($cb);
#}
#
#sub set_on_enable_action_buttons {
#    my ($self, $cb) = @_;
#    $self->on_enable_action_buttons($cb);
#}
#
#sub update_volumes_selection {
#    my ($self) = @_;
#
#    foreach my $obj_idx (0..$#{$self->{model}->objects}) {
#        if ($self->{objects}[$obj_idx]->selected) {
#            my $volume_idxs = $self->{objects_volumes_idxs}->[$obj_idx];
#            $self->select_volume($_) for @{$volume_idxs};
#        }
#    }
#}
#
#sub reload_scene {
#    my ($self, $force) = @_;
#
#    $self->reset_objects;
#    $self->update_bed_size;
#
#    if (! $self->IsShown && ! $force) {
#        $self->{reload_delayed} = 1;
#        return;
#    }
#
#    $self->{reload_delayed} = 0;
#
#    $self->{objects_volumes_idxs} = [];    
#    foreach my $obj_idx (0..$#{$self->{model}->objects}) {
#        my @volume_idxs = $self->load_object($self->{model}, $self->{print}, $obj_idx);
#        push(@{$self->{objects_volumes_idxs}}, \@volume_idxs);
#    }
#    
#    $self->update_volumes_selection;
#        
#    if (defined $self->{config}->nozzle_diameter) {
#        # Should the wipe tower be visualized?
#        my $extruders_count = scalar @{ $self->{config}->nozzle_diameter };
#        # Height of a print.
#        my $height = $self->{model}->bounding_box->z_max;
#        # Show at least a slab.
#        $height = 10 if $height < 10;
#        if ($extruders_count > 1 && $self->{config}->single_extruder_multi_material && $self->{config}->wipe_tower &&
#            ! $self->{config}->complete_objects) {
#            $self->volumes->load_wipe_tower_preview(1000, 
#                $self->{config}->wipe_tower_x, $self->{config}->wipe_tower_y, $self->{config}->wipe_tower_width,
#		#$self->{config}->wipe_tower_per_color_wipe# 15 * ($extruders_count - 1), # this is just a hack when the config parameter became obsolete
#		15 * ($extruders_count - 1),
#                $self->{model}->bounding_box->z_max, $self->{config}->wipe_tower_rotation_angle, $self->UseVBOs);
#        }
#    }
#
#    $self->update_volumes_colors_by_extruder($self->{config});
#    
#    # checks for geometry outside the print volume to render it accordingly
#    if (scalar @{$self->volumes} > 0)
#    {
#        my $contained = $self->volumes->check_outside_state($self->{config});
#        if (!$contained) {
#            $self->set_warning_enabled(1);
#            Slic3r::GUI::_3DScene::generate_warning_texture(L("Detected object outside print volume"));
#            $self->on_enable_action_buttons->(0) if ($self->on_enable_action_buttons);
#        } else {
#            $self->set_warning_enabled(0);
#            $self->volumes->reset_outside_state();
#            Slic3r::GUI::_3DScene::reset_warning_texture();
#            $self->on_enable_action_buttons->(scalar @{$self->{model}->objects} > 0) if ($self->on_enable_action_buttons);
#        }
#    } else {
#        $self->set_warning_enabled(0);
#        Slic3r::GUI::_3DScene::reset_warning_texture();
#    }
#}
#
#sub update_bed_size {
#    my ($self) = @_;
#    $self->set_bed_shape($self->{config}->bed_shape);
#}
#
## Called by the Platter wxNotebook when this page is activated.
#sub OnActivate {
#    my ($self) = @_;
#    $self->reload_scene(1) if ($self->{reload_delayed});
#}
#==============================================================================================================================

1;
