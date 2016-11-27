# Generate an anonymous or "lambda" 3D object. This gets used with the Add Generic option in Settings.
# 

package Slic3r::GUI::Plater::LambdaObjectDialog;
use strict;
use warnings;
use utf8;

use Slic3r::Geometry qw(PI X);
use Wx qw(wxTheApp :dialog :id :misc :sizer wxTAB_TRAVERSAL wxCB_READONLY wxTE_PROCESS_TAB);
use Wx::Event qw(EVT_CLOSE EVT_BUTTON EVT_COMBOBOX EVT_TEXT);
use Scalar::Util qw(looks_like_number);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Lambda Object", wxDefaultPosition, [500,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    # Note whether the window was already closed, so a pending update is not executed.
    $self->{already_closed} = 0;
    $self->{object_parameters} = { 
        type => "box",
        dim => [1, 1, 1],
    };

    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    my $button_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
    EVT_BUTTON($self, wxID_OK, sub {
        # validate user input
        return if !$self->CanClose;
        
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    $button_sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);

    my @options = ("box");
    $self->{type} = Wx::ComboBox->new($self, 1, "box", wxDefaultPosition, wxDefaultSize, \@options, wxCB_READONLY);

    my $sbox = Wx::StaticBox->new($self, -1, '', wxDefaultPosition, wxDefaultSize, 0, 'sbox');
    my $cube_dim_sizer = Wx::StaticBoxSizer->new($sbox, wxVERTICAL);
    {
        my $opt_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        {
            my $lbl = Wx::StaticText->new($self, 2, "X", wxDefaultPosition, Wx::Size->new(10,-1));
            $self->{dim_x} = Wx::TextCtrl->new($self, 2, "1", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_TAB);
            $opt_sizer->Add($lbl, 1, wxRIGHT , 8);
            $opt_sizer->Add($self->{dim_x});

        }
        $cube_dim_sizer->Add($opt_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
        $opt_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        {
            my $lbl = Wx::StaticText->new($self, -1, "Y", wxDefaultPosition, Wx::Size->new(10,-1));
            $self->{dim_y} = Wx::TextCtrl->new($self, 2, "1", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_TAB);
            $opt_sizer->Add($lbl, 1, wxRIGHT , 8);
            $opt_sizer->Add($self->{dim_y});
        }
        $cube_dim_sizer->Add($opt_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
        $opt_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        {
            my $lbl = Wx::StaticText->new($self, -1, "Z", wxDefaultPosition, Wx::Size->new(10,-1));
            $self->{dim_z} = Wx::TextCtrl->new($self, 2, "1", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_TAB);
            $opt_sizer->Add($lbl, 1, wxRIGHT , 8);
            $opt_sizer->Add($self->{dim_z});
        }
        $cube_dim_sizer->Add($opt_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
        EVT_TEXT($self, 2, sub { 
                if (!looks_like_number($self->{dim_x}->GetValue)) {
                return 0;
                }
                if (!looks_like_number($self->{dim_y}->GetValue)) {
                return 0;
                }
                if (!looks_like_number($self->{dim_z}->GetValue)) {
                return 0;
                }
                if ($self->{dim_x}->GetValue() > 0) {
                $self->{object_parameters}->{dim}[0] = $self->{dim_x}->GetValue;
                }
                if ($self->{dim_y}->GetValue() > 0) {
                $self->{object_parameters}->{dim}[1] = $self->{dim_y}->GetValue;
                }
                if ($self->{dim_z}->GetValue() > 0) {
                $self->{object_parameters}->{dim}[2] = $self->{dim_z}->GetValue;
                }
        });
    }
    EVT_COMBOBOX($self, 1, sub{ 
        $self->{object_parameters}->{type} = $self->{type}->GetValue();
    });
    $self->{sizer}->Add($self->{type}, 0, wxEXPAND, 3);
    $self->{sizer}->Add($cube_dim_sizer, 0, wxEXPAND, 10);
    $self->{sizer}->Add($button_sizer);
    $self->SetSizer($self->{sizer});
    $self->{sizer}->Fit($self);
    $self->{sizer}->SetSizeHints($self);

    
    return $self;
}
sub CanClose {
    return 1;
}
sub ObjectParameter {
    my ($self) = @_;
    return $self->{object_parameters};
}
1;
