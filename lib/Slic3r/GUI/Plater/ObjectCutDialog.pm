package Slic3r::GUI::Plater::ObjectCutDialog;
use strict;
use warnings;
use utf8;

use Slic3r::Geometry qw(PI);
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
    my $cut_button_sizer = Wx::BoxSizer->new(wxVERTICAL);
    {
        $self->{btn_cut} = Wx::Button->new($self, -1, "Perform cut", wxDefaultPosition, wxDefaultSize);
        $cut_button_sizer->Add($self->{btn_cut}, 0, wxALIGN_RIGHT | wxALL, 10);
    }
    my $optgroup = Slic3r::GUI::OptionsGroup->new(
        parent  => $self,
        title   => 'Cut',
        options => [
            {
                opt_key     => 'z',
                type        => 'slider',
                label       => 'Z',
                default     => $self->{cut_options}{z},
                min         => 0,
                max         => $self->{model_object}->bounding_box->size->z,
            },
            {
                opt_key     => 'keep_upper',
                type        => 'bool',
                label       => 'Upper part',
                default     => $self->{cut_options}{keep_upper},
            },
            {
                opt_key     => 'keep_lower',
                type        => 'bool',
                label       => 'Lower part',
                default     => $self->{cut_options}{keep_lower},
            },
            {
                opt_key     => 'rotate_lower',
                type        => 'bool',
                label       => '',
                tooltip     => 'If enabled, the lower part will be rotated by 180° so that the flat cut surface lies on the print bed.',
                default     => $self->{cut_options}{rotate_lower},
            },
        ],
        lines => [
            {
                label       => 'Z',
                options     => [qw(z)],
            },
            {
                label       => 'Keep',
                options     => [qw(keep_upper keep_lower)],
            },
            {
                label       => 'Rotate lower part',
                options     => [qw(rotate_lower)],
            },
            {
                sizer => $cut_button_sizer,
            },
        ],
        on_change => sub {
            my ($opt_key, $value) = @_;
            
            $self->{cut_options}{$opt_key} = $value;
            if ($opt_key eq 'z') {
                if ($self->{canvas}) {
                    $self->{canvas}->SetCuttingPlane($value);
                    $self->{canvas}->Render;
                }
            }
        },
        label_width => 120,
    );
    
    # left pane with tree
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $left_sizer->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    # right pane with preview canvas
    my $canvas;
    if ($Slic3r::GUI::have_OpenGL) {
        $canvas = $self->{canvas} = Slic3r::GUI::PreviewCanvas->new($self, $self->{model_object});
        $canvas->SetSize([500,500]);
        $canvas->SetMinSize($canvas->GetSize);
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
    
    return $self;
}

sub perform_cut {
    my ($self) = @_;
    
    my ($upper_object, $lower_object) = $self->{model_object}->cut($self->{cut_options}{z});
    $self->{new_model_objects} = [];
    if ($self->{cut_options}{keep_upper} && defined $upper_object) {
        push @{$self->{new_model_objects}}, $upper_object;
    }
    if ($self->{cut_options}{keep_lower} && defined $lower_object) {
        push @{$self->{new_model_objects}}, $lower_object;
        if ($self->{cut_options}{rotate_lower}) {
            $lower_object->rotate_x(PI);
        }
    }
    
    $self->Close;
}

sub NewModelObjects {
    my ($self) = @_;
    return @{ $self->{new_model_objects} };
}

1;
