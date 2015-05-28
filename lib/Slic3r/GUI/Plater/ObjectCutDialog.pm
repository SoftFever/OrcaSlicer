package Slic3r::GUI::Plater::ObjectCutDialog;
use strict;
use warnings;
use utf8;

use Slic3r::Geometry qw(PI X);
use Wx qw(:dialog :id :misc :sizer wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE EVT_BUTTON);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, $params{object}->name, wxDefaultPosition, [500,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    $self->{model_object_idx} = $params{model_object_idx};
    $self->{model_object} = $params{model_object};
    $self->{new_model_objects} = [];
    
    # cut options
    $self->{cut_options} = {
        z               => 0,
        keep_upper      => 1,
        keep_lower      => 1,
        rotate_lower    => 1,
    };
    
    my $optgroup;
    $optgroup = $self->{optgroup} = Slic3r::GUI::OptionsGroup->new(
        parent      => $self,
        title       => 'Cut',
        on_change   => sub {
            my ($opt_id) = @_;
            
            $self->{cut_options}{$opt_id} = $optgroup->get_value($opt_id);
            $self->_update;
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
        $canvas->enable_cutting(1);
        $canvas->load_object($self->{model_object}, undef, [0]);
        $canvas->set_auto_bed_shape;
        $canvas->SetSize([500,500]);
        $canvas->SetMinSize($canvas->GetSize);
        $canvas->zoom_to_volumes;
    }
    
    $self->{sizer} = Wx::BoxSizer->new(wxHORIZONTAL);
    $self->{sizer}->Add($left_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);
    $self->{sizer}->Add($canvas, 1, wxEXPAND | wxALL, 0) if $canvas;
    
    $self->SetSizer($self->{sizer});
    $self->SetMinSize($self->GetSize);
    $self->{sizer}->SetSizeHints($self);
    
    # needed to actually free memory
    EVT_CLOSE($self, sub {
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    
    EVT_BUTTON($self, $self->{btn_cut}, sub { $self->perform_cut });
    
    $self->_update;
    
    return $self;
}

sub _update {
    my ($self) = @_;
    
    my $optgroup = $self->{optgroup};
    
    # update canvas
    if ($self->{canvas}) {
        $self->{canvas}->SetCuttingPlane($self->{cut_options}{z});
        $self->{canvas}->Render;
    }
    
    # update controls
    my $z = $self->{cut_options}{z};
    $optgroup->get_field('keep_upper')->toggle(my $have_upper = abs($z - $optgroup->get_option('z')->max) > 0.1);
    $optgroup->get_field('keep_lower')->toggle(my $have_lower = $z > 0.1);
    $optgroup->get_field('rotate_lower')->toggle($z > 0 && $self->{cut_options}{keep_lower});
    
    # update cut button
    if (($self->{cut_options}{keep_upper} && $have_upper)
        || ($self->{cut_options}{keep_lower} && $have_lower)) {
        $self->{btn_cut}->Enable;
    } else {
        $self->{btn_cut}->Disable;
    }
}

sub perform_cut {
    my ($self) = @_;
    
    # scale Z down to original size since we're using the transformed mesh for 3D preview
    # and cut dialog but ModelObject::cut() needs Z without any instance transformation
    my $z = $self->{cut_options}{z} / $self->{model_object}->instances->[0]->scaling_factor;
    
    my ($new_model) = $self->{model_object}->cut($z);
    my ($upper_object, $lower_object) = @{$new_model->objects};
    $self->{new_model} = $new_model;
    $self->{new_model_objects} = [];
    if ($self->{cut_options}{keep_upper} && $upper_object->volumes_count > 0) {
        $upper_object->center_around_origin;  # align to Z = 0
        push @{$self->{new_model_objects}}, $upper_object;
    }
    if ($self->{cut_options}{keep_lower} && $lower_object->volumes_count > 0) {
        push @{$self->{new_model_objects}}, $lower_object;
        if ($self->{cut_options}{rotate_lower}) {
            $lower_object->rotate(PI, X);
            $lower_object->center_around_origin;  # align to Z = 0
        }
    }
    
    $self->Close;
}

sub NewModelObjects {
    my ($self) = @_;
    return @{ $self->{new_model_objects} };
}

1;
