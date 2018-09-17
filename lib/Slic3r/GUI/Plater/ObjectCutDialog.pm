# Cut an object at a Z position, keep either the top or the bottom of the object.
# This dialog gets opened with the "Cut..." button above the platter.

package Slic3r::GUI::Plater::ObjectCutDialog;
use strict;
use warnings;
use utf8;

use Slic3r::Geometry qw(PI X);
use Wx qw(wxTheApp :dialog :id :misc :sizer wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE EVT_BUTTON);
use List::Util qw(max);
use base 'Wx::Dialog';

sub new {
    my ($class, $parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, $params{object}->name, wxDefaultPosition, [500,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    $self->{model_object} = $params{model_object};
    $self->{new_model_objects} = [];
    # Mark whether the mesh cut is valid.
    # If not, it needs to be recalculated by _update() on wxTheApp->CallAfter() or on exit of the dialog.
    $self->{mesh_cut_valid} = 0;
    # Note whether the window was already closed, so a pending update is not executed.
    $self->{already_closed} = 0;
    
    # cut options
    $self->{cut_options} = {
        z               => 0,
        keep_upper      => 1,
        keep_lower      => 1,
        rotate_lower    => 1,
#        preview         => 1,
# Disabled live preview by default as it is not stable and/or the calculation takes too long for interactive usage.
        preview         => 0,
    };
    
    my $optgroup;
    $optgroup = $self->{optgroup} = Slic3r::GUI::OptionsGroup->new(
        parent      => $self,
        title       => 'Cut',
        on_change   => sub {
            my ($opt_id) = @_;
            # There seems to be an issue with wxWidgets 3.0.2/3.0.3, where the slider
            # genates tens of events for a single value change.
            # Only trigger the recalculation if the value changes
            # or a live preview was activated and the mesh cut is not valid yet.
            if ($self->{cut_options}{$opt_id} != $optgroup->get_value($opt_id) ||
                ! $self->{mesh_cut_valid} && $self->_life_preview_active()) {
                $self->{cut_options}{$opt_id} = $optgroup->get_value($opt_id);
                $self->{mesh_cut_valid} = 0;
                wxTheApp->CallAfter(sub {
                    $self->_update;
                });
            }
        },
        label_width  => 120,
    );
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'z',
        type        => 'slider',
        label       => 'Z',
        default     => $self->{cut_options}{z},
        min         => 0,
        max         => $self->{model_object}->bounding_box->size->z,
        full_width  => 1,
    ));
    {
        my $line = Slic3r::GUI::OptionsGroup::Line->new(
            label => 'Keep',
        );
        $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
            opt_id  => 'keep_upper',
            type    => 'bool',
            label   => 'Upper part',
            default => $self->{cut_options}{keep_upper},
        ));
        $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
            opt_id  => 'keep_lower',
            type    => 'bool',
            label   => 'Lower part',
            default => $self->{cut_options}{keep_lower},
        ));
        $optgroup->append_line($line);
    }
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'rotate_lower',
        label       => 'Rotate lower part upwards',
        type        => 'bool',
        tooltip     => 'If enabled, the lower part will be rotated by 180° so that the flat cut surface lies on the print bed.',
        default     => $self->{cut_options}{rotate_lower},
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'preview',
        label       => 'Show preview',
        type        => 'bool',
        tooltip     => 'If enabled, object will be cut in real time.',
        default     => $self->{cut_options}{preview},
    ));
    {
        my $cut_button_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $self->{btn_cut} = Wx::Button->new($self, -1, "Perform cut", wxDefaultPosition, wxDefaultSize);
        $cut_button_sizer->Add($self->{btn_cut}, 0, wxALIGN_RIGHT | wxALL, 10);
        $optgroup->append_line(Slic3r::GUI::OptionsGroup::Line->new(
            sizer => $cut_button_sizer,
        ));
    }
    
    # left pane with tree
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $left_sizer->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    # right pane with preview canvas
    my $canvas;
    if ($Slic3r::GUI::have_OpenGL) {
        $canvas = $self->{canvas} = Slic3r::GUI::3DScene->new($self);
        Slic3r::GUI::_3DScene::load_model_object($self->{canvas}, $self->{model_object}, 0, [0]);
        Slic3r::GUI::_3DScene::set_auto_bed_shape($canvas);
        Slic3r::GUI::_3DScene::set_axes_length($canvas, 2.0 * max(@{ Slic3r::GUI::_3DScene::get_volumes_bounding_box($canvas)->size }));
        $canvas->SetSize([500,500]);
        $canvas->SetMinSize($canvas->GetSize);
        Slic3r::GUI::_3DScene::set_config($canvas, $self->GetParent->{config});
        Slic3r::GUI::_3DScene::enable_force_zoom_to_bed($canvas, 1);
    }
    
    $self->{sizer} = Wx::BoxSizer->new(wxHORIZONTAL);
    $self->{sizer}->Add($left_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);
    $self->{sizer}->Add($canvas, 1, wxEXPAND | wxALL, 0) if $canvas;
    
    $self->SetSizer($self->{sizer});
    $self->SetMinSize($self->GetSize);
    $self->{sizer}->SetSizeHints($self);
    
    EVT_BUTTON($self, $self->{btn_cut}, sub {
        # Recalculate the cut if the preview was not active.
        $self->_perform_cut() unless $self->{mesh_cut_valid};

        # Adjust position / orientation of the split object halves.
        if ($self->{new_model_objects}{lower}) {
            if ($self->{cut_options}{rotate_lower}) {
                $self->{new_model_objects}{lower}->rotate(PI, Slic3r::Pointf3->new(1,0,0));
                $self->{new_model_objects}{lower}->center_around_origin;  # align to Z = 0
            }
        }
        if ($self->{new_model_objects}{upper}) {
            $self->{new_model_objects}{upper}->center_around_origin;  # align to Z = 0
        }
        
        # Note that the window was already closed, so a pending update will not be executed.
        $self->{already_closed} = 1;
        $self->EndModal(wxID_OK);
        $self->{canvas}->Destroy;
        $self->Destroy();
    });

    EVT_CLOSE($self, sub {
        # Note that the window was already closed, so a pending update will not be executed.
        $self->{already_closed} = 1;
        $self->EndModal(wxID_CANCEL);
        $self->{canvas}->Destroy;
        $self->Destroy();
    });

    $self->_update;
    
    return $self;
}

# scale Z down to original size since we're using the transformed mesh for 3D preview
# and cut dialog but ModelObject::cut() needs Z without any instance transformation
sub _mesh_slice_z_pos
{
    my ($self) = @_;
    return $self->{cut_options}{z} / $self->{model_object}->instances->[0]->scaling_factor;
}

# Only perform live preview if just a single part of the object shall survive.
sub _life_preview_active
{
    my ($self) = @_;
    return $self->{cut_options}{preview} && ($self->{cut_options}{keep_upper} != $self->{cut_options}{keep_lower});
}

# Slice the mesh, keep the top / bottom part.
sub _perform_cut 
{
    my ($self) = @_;

    # Early exit. If the cut is valid, don't recalculate it.
    return if $self->{mesh_cut_valid};

    my $z = $self->_mesh_slice_z_pos();

    my ($new_model) = $self->{model_object}->cut($z);
    my ($upper_object, $lower_object) = @{$new_model->objects};
    $self->{new_model} = $new_model;
    $self->{new_model_objects} = {};
    if ($self->{cut_options}{keep_upper} && $upper_object->volumes_count > 0) {
        $self->{new_model_objects}{upper} = $upper_object;
    }
    if ($self->{cut_options}{keep_lower} && $lower_object->volumes_count > 0) {
        $self->{new_model_objects}{lower} = $lower_object;
    }

    $self->{mesh_cut_valid} = 1;
}

sub _update {
    my ($self) = @_;

    # Don't update if the window was already closed.
    # We are not sure whether the action planned by wxTheApp->CallAfter() may be triggered after the window is closed.
    # Probably not, but better be safe than sorry, which is espetially true on multiple platforms.
    return if $self->{already_closed};

    # Only recalculate the cut, if the live cut preview is active.
    my $life_preview_active = $self->_life_preview_active();
    $self->_perform_cut() if $life_preview_active;

    {
        # scale Z down to original size since we're using the transformed mesh for 3D preview
        # and cut dialog but ModelObject::cut() needs Z without any instance transformation
        my $z = $self->_mesh_slice_z_pos();

        
        # update canvas
        if ($self->{canvas}) {
            # get volumes to render
            my @objects = ();
            if ($life_preview_active) {
                push @objects, values %{$self->{new_model_objects}};
            } else {
                push @objects, $self->{model_object};
            }
        
            my $z_cut = $z + $self->{model_object}->bounding_box->z_min;        
        
            # get section contour
            my @expolygons = ();
            foreach my $volume (@{$self->{model_object}->volumes}) {
                next if !$volume->mesh;
                next if !$volume->model_part;
                my $expp = $volume->mesh->slice([ $z_cut ])->[0];
                push @expolygons, @$expp;
            }
            foreach my $expolygon (@expolygons) {
                $self->{model_object}->instances->[0]->transform_polygon($_)
                    for @$expolygon;
                $expolygon->translate(map Slic3r::Geometry::scale($_), @{ $self->{model_object}->instances->[0]->offset });
            }

            Slic3r::GUI::_3DScene::reset_volumes($self->{canvas});
            Slic3r::GUI::_3DScene::load_model_object($self->{canvas}, $_, 0, [0]) for @objects;
            Slic3r::GUI::_3DScene::set_cutting_plane($self->{canvas}, $self->{cut_options}{z}, [@expolygons]);
            Slic3r::GUI::_3DScene::update_volumes_colors_by_extruder($self->{canvas});
            Slic3r::GUI::_3DScene::render($self->{canvas});
        }
    }
    
    # update controls
    {
        my $z = $self->{cut_options}{z};
        my $optgroup = $self->{optgroup};
        $optgroup->get_field('keep_upper')->toggle(my $have_upper = abs($z - $optgroup->get_option('z')->max) > 0.1);
        $optgroup->get_field('keep_lower')->toggle(my $have_lower = $z > 0.1);
        $optgroup->get_field('rotate_lower')->toggle($z > 0 && $self->{cut_options}{keep_lower});
# Disabled live preview by default as it is not stable and/or the calculation takes too long for interactive usage.
#        $optgroup->get_field('preview')->toggle($self->{cut_options}{keep_upper} != $self->{cut_options}{keep_lower});
    
        # update cut button
        if (($self->{cut_options}{keep_upper} && $have_upper)
            || ($self->{cut_options}{keep_lower} && $have_lower)) {
            $self->{btn_cut}->Enable;
        } else {
            $self->{btn_cut}->Disable;
        }
    }
}

sub NewModelObjects {
    my ($self) = @_;
    return values %{ $self->{new_model_objects} };
}

1;
